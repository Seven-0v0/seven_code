import { lstat, readFile, realpath } from "node:fs/promises"
import { extname, relative, resolve, sep } from "node:path"

import {
  classifyProviderResponse,
  parseResponseBody,
  sanitizeText,
  type ProviderResponse,
} from "./video-probe-response"

export { classifyProviderResponse, sanitizeText, type ProviderResponse } from "./video-probe-response"

export const DEFAULT_PROMPT =
  "Describe the complete video chronologically. State whether the sequence is 点赞 then V/剪刀手, with approximate timestamps."
export const MAXIMUM_VIDEO_BYTES = 8 * 1024 * 1024
export const REQUEST_TIMEOUT_MS = 60_000

export type ModelReference = {
  readonly providerID: string
  readonly modelID: string
}

export type ModelSelection =
  | { readonly kind: "ready"; readonly providerID: string; readonly modelID: string }
  | { readonly kind: "missing_model_configuration"; readonly message: string }

export type PreparedVideo = {
  readonly kind: "ready"
  readonly absolutePath: string
  readonly dataUrl: string
}

export type VideoPreparationError = {
  readonly kind: "invalid_path" | "invalid_video" | "payload_too_large"
  readonly message: string
}

export type VideoPreparationResult = PreparedVideo | VideoPreparationError

export type ProbeResult =
  | ProviderResponse
  | VideoPreparationError
  | { readonly kind: "request_failed"; readonly message: string }

type VideoPayloadInput = {
  readonly model: string
  readonly prompt: string
  readonly videoDataUrl: string
}

type ProbeInput = {
  readonly endpoint: string
  readonly filePath: string
  readonly headers?: Readonly<Record<string, string>>
  readonly maximumBytes: number
  readonly model: string
  readonly prompt: string
  readonly signal?: AbortSignal
  readonly timeoutMs?: number
  readonly workspace: string
  readonly worktree?: string
  readonly debug?: boolean
}

export function buildChatCompletionsEndpoint(baseUrl: string): string {
  const url = new URL(baseUrl)
  const path = url.pathname.replace(/\/+$/, "")
  const endpointPath = path.endsWith("/chat/completions")
    ? path
    : path.endsWith("/v1")
      ? `${path}/chat/completions`
      : `${path}/v1/chat/completions`

  url.pathname = endpointPath || "/v1/chat/completions"
  url.search = ""
  url.hash = ""
  return url.toString()
}

export function resolveModelSelection(input: {
  readonly observed: ModelReference | undefined
  readonly override: string | undefined
}): ModelSelection {
  const override = input.override?.trim()
  if (override) {
    const separator = override.indexOf("/")
    if (separator > 0 && separator < override.length - 1) {
      return {
        kind: "ready",
        providerID: override.slice(0, separator),
        modelID: override.slice(separator + 1),
      }
    }

    if (input.observed) {
      return { kind: "ready", providerID: input.observed.providerID, modelID: override }
    }
  }

  if (input.observed) {
    return { kind: "ready", ...input.observed }
  }

  return {
    kind: "missing_model_configuration",
    message:
      "No model is known for this session. Send a normal message first, or pass model as providerID/modelID.",
  }
}

export async function prepareWorkspaceVideo(input: {
  readonly workspace: string
  readonly worktree?: string
  readonly filePath: string
  readonly maximumBytes: number
}): Promise<VideoPreparationResult> {
  const workspace = resolve(input.workspace)
  const worktree = resolve(input.worktree ?? input.workspace)
  const absolutePath = resolve(workspace, input.filePath)
  if (!isWithin(workspace, absolutePath)) {
    return { kind: "invalid_path", message: "file_path must stay within the workspace" }
  }
  if (!isWithin(worktree, absolutePath)) {
    return { kind: "invalid_path", message: "file_path must stay within the workspace worktree" }
  }

  if (extname(absolutePath).toLowerCase() !== ".mp4") {
    return { kind: "invalid_video", message: "file_path must reference an existing MP4 file" }
  }

  try {
    const details = await lstat(absolutePath)
    const resolvedWorkspace = await realpath(workspace)
    const resolvedWorktree = await realpath(worktree)
    const resolvedVideo = await realpath(absolutePath)
    if (!isWithin(resolvedWorkspace, resolvedVideo)) {
      return { kind: "invalid_path", message: "file_path must stay within the workspace" }
    }
    if (!isWithin(resolvedWorktree, resolvedVideo)) {
      return { kind: "invalid_path", message: "file_path must stay within the workspace worktree" }
    }
    if (!details.isFile()) {
      return { kind: "invalid_video", message: "file_path must reference an existing MP4 file" }
    }
    if (details.size > input.maximumBytes) {
      return {
        kind: "payload_too_large",
        message: `MP4 exceeds the ${input.maximumBytes}-byte safety limit`,
      }
    }
    const bytes = await readFile(absolutePath)
    return {
      kind: "ready",
      absolutePath,
      dataUrl: `data:video/mp4;base64,${bytes.toString("base64")}`,
    }
  } catch (error) {
    if (!(error instanceof Error)) {
      return { kind: "invalid_video", message: "file_path must reference an existing MP4 file" }
    }
    return { kind: "invalid_video", message: "file_path must reference an existing MP4 file" }
  }
}

function isWithin(root: string, candidate: string): boolean {
  const path = relative(root, candidate)
  return path !== ".." && !path.startsWith(`..${sep}`)
}

export function buildVideoPayload(input: VideoPayloadInput): {
  readonly model: string
  readonly messages: readonly [{ readonly role: "user"; readonly content: readonly unknown[] }]
} {
  return {
    model: input.model,
    messages: [
      {
        role: "user",
        content: [
          { type: "text", text: input.prompt },
          { type: "video_url", video_url: { url: input.videoDataUrl } },
        ],
      },
    ],
  }
}

export async function probeVideo(input: ProbeInput): Promise<ProbeResult> {
  const video = await prepareWorkspaceVideo(input)
  if (video.kind !== "ready") return video

  try {
    const response = await fetch(buildChatCompletionsEndpoint(input.endpoint), {
      method: "POST",
      headers: { "content-type": "application/json", ...input.headers },
      body: JSON.stringify(
        buildVideoPayload({
          model: input.model,
          prompt: input.prompt,
          videoDataUrl: video.dataUrl,
        }),
      ),
      signal: requestSignal(input.signal, input.timeoutMs ?? REQUEST_TIMEOUT_MS),
    })
    const responseText = await response.text()
    const body = parseResponseBody(responseText)
    
    if (input.debug) {
      return {
        kind: "success",
        text: `[DEBUG] HTTP ${response.status}\n[DEBUG] Response:\n${responseText.slice(0, 2000)}`,
      }
    }
    
    return classifyProviderResponse({ status: response.status, body })
  } catch (error) {
    if (error instanceof DOMException && error.name === "TimeoutError") {
      return { kind: "request_failed", message: "request timed out" }
    }
    if (error instanceof DOMException && error.name === "AbortError") {
      return { kind: "request_failed", message: "request was cancelled" }
    }
    if (error instanceof Error) {
      return { kind: "request_failed", message: sanitizeText(error.message) }
    }
    return { kind: "request_failed", message: "network request failed" }
  }
}

function requestSignal(signal: AbortSignal | undefined, timeoutMs: number): AbortSignal {
  const timeout = AbortSignal.timeout(timeoutMs)
  return signal ? AbortSignal.any([signal, timeout]) : timeout
}
