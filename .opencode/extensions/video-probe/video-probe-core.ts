import { lstat, readFile, realpath } from "node:fs/promises"
import { basename, extname, relative, resolve, sep } from "node:path"

import { z } from "zod"

import {
  classifyProviderResponse,
  parseResponseBody,
  sanitizeText,
  type ProbeTimings,
  type ProbeUsage,
  type ProviderResponse,
} from "./video-probe-response.js"

export {
  classifyProviderResponse,
  sanitizeText,
  type ProbeDiagnostics,
  type ProbeTimings,
  type ProbeUsage,
  type ProviderResponse,
} from "./video-probe-response.js"

export const DEFAULT_PROMPT =
  "Describe the complete video chronologically. State whether the sequence is 点赞 then V/剪刀手, with approximate timestamps."
export const REQUEST_TIMEOUT_MS = 300_000
export const DASHSCOPE_HOST = "dashscope.aliyuncs.com"

export const DEFAULT_FPS = 2.0
export const MINIMUM_FPS = 0.1
export const MAXIMUM_FPS = 10

export const FpsSchema = z.number().min(MINIMUM_FPS).max(MAXIMUM_FPS).default(DEFAULT_FPS)

export const TimeoutMsSchema = z.number().int().positive()
const UploadPolicyNumberSchema = z.preprocess(
  (value) => (typeof value === "string" ? Number(value) : value),
  z.number().finite().positive(),
)

const UploadPolicyDataSchema = z.object({
  policy: z.string(),
  signature: z.string(),
  upload_dir: z.string().min(1),
  upload_host: z.string().url().refine((value) => new URL(value).protocol === "https:", {
    message: "upload_host must use https",
  }),
  oss_access_key_id: z.string().min(1),
  x_oss_object_acl: z.string().min(1),
  x_oss_forbid_overwrite: z.string().min(1),
  max_file_size_mb: UploadPolicyNumberSchema,
  expire_in_seconds: UploadPolicyNumberSchema,
})

const UploadPolicyResponseSchema = z.object({
  request_id: z.string().min(1),
  data: UploadPolicyDataSchema,
})

export type UploadPolicy = z.infer<typeof UploadPolicyResponseSchema>["data"]

export type UploadPolicyResult =
  | { readonly kind: "ready"; readonly policy: UploadPolicy }
  | { readonly kind: "http_rejection"; readonly status: number; readonly message: string }
  | { readonly kind: "invalid_json"; readonly message: string }
  | { readonly kind: "invalid_policy"; readonly message: string }
  | { readonly kind: "invalid_upload_host"; readonly message: string }
  | { readonly kind: "request_failed"; readonly message: string }

export type OssUploadResult =
  | { readonly kind: "ready"; readonly ossUrl: string; readonly key: string }
  | { readonly kind: "http_rejection"; readonly status: number; readonly message: string }
  | { readonly kind: "invalid_file_name"; readonly message: string }
  | { readonly kind: "request_failed"; readonly message: string }

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
  readonly sizeBytes: number
}

export type VideoPreparationError = {
  readonly kind: "invalid_path" | "invalid_video"
  readonly message: string
}

export type VideoPreparationResult = PreparedVideo | VideoPreparationError

export type ProbeResult =
  | ProviderResponse
  | VideoPreparationError
  | { readonly kind: "unsupported_provider"; readonly message: string }
  | { readonly kind: "policy_unavailable"; readonly message: string }
  | { readonly kind: "upload_failed"; readonly message: string }
  | { readonly kind: "request_failed"; readonly message: string }

type VideoPayloadInput = {
  readonly model: string
  readonly prompt: string
  readonly videoUrl: string
  readonly fps: number
}

type ProbeInput = {
  readonly endpoint: string
  readonly filePath: string
  readonly headers?: Readonly<Record<string, string>>
  readonly model: string
  readonly prompt: string
  readonly fps: number
  readonly signal?: AbortSignal
  readonly timeoutMs?: number
  readonly workspace: string
  readonly worktree?: string
  readonly debug?: boolean
  readonly fetchImpl?: UploadPolicyFetch
  readonly now?: () => number
}

type UploadPolicyInput = {
  readonly model: string
  readonly headers?: Readonly<Record<string, string>>
  readonly signal?: AbortSignal
  readonly fetchImpl?: UploadPolicyFetch
  readonly endpoint?: string
}

type UploadPolicyFetch = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>

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
    return { kind: "ready", absolutePath, sizeBytes: details.size }
  } catch {
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
          { type: "video_url", video_url: { url: input.videoUrl }, fps: input.fps },
        ],
      },
    ],
  }
}

function buildChatHeaders(headers: Readonly<Record<string, string>> | undefined): Headers {
  const result = new Headers(headers)
  result.set("content-type", "application/json")
  result.set("X-DashScope-OssResourceResolve", "enable")
  return result
}

export async function getUploadPolicy(input: UploadPolicyInput): Promise<UploadPolicyResult> {
  try {
    const url = new URL(input.endpoint ?? "https://dashscope.aliyuncs.com/api/v1/uploads")
    url.search = new URLSearchParams({ action: "getPolicy", model: input.model }).toString()
    const requestInit: RequestInit = { method: "GET" }
    if (input.signal) requestInit.signal = input.signal
    if (input.headers) requestInit.headers = input.headers
    const response = await (input.fetchImpl ?? fetch)(url, requestInit)
    const responseText = await response.text()
    if (!response.ok) {
      return {
        kind: "http_rejection",
        status: response.status,
        message: sanitizeText(responseText) || "DashScope rejected the upload policy request",
      }
    }
    return parseUploadPolicyResponse(responseText)
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

type UploadToOssInput = {
  readonly policy: UploadPolicy
  readonly absolutePath: string
  readonly signal?: AbortSignal
  readonly fetchImpl?: UploadPolicyFetch
}

export async function uploadToOss(input: UploadToOssInput): Promise<OssUploadResult> {
  const fileName = basename(input.absolutePath)
  if (!isSafeUploadFileName(fileName)) {
    return { kind: "invalid_file_name", message: "absolutePath must reference a real file name" }
  }

  try {
    const bytes = await readFile(input.absolutePath)
    const uploadKey = buildUploadKey(input.policy.upload_dir, fileName)
    const formData = new FormData()
    formData.append("OSSAccessKeyId", input.policy.oss_access_key_id)
    formData.append("Signature", input.policy.signature)
    formData.append("policy", input.policy.policy)
    formData.append("x-oss-object-acl", input.policy.x_oss_object_acl)
    formData.append("x-oss-forbid-overwrite", input.policy.x_oss_forbid_overwrite)
    formData.append("key", uploadKey)
    formData.append("success_action_status", "200")
    formData.append("file", new Blob([bytes], { type: "video/mp4" }), fileName)

    const requestInit: RequestInit = { method: "POST", body: formData }
    if (input.signal) requestInit.signal = input.signal

    const response = await (input.fetchImpl ?? fetch)(input.policy.upload_host, requestInit)

    if (!response.ok) {
      return {
        kind: "http_rejection",
        status: response.status,
        message: sanitizeUploadFailureMessage(await response.text()) || "OSS upload rejected the request",
      }
    }

    return { kind: "ready", key: uploadKey, ossUrl: `oss://${uploadKey}` }
  } catch (error) {
    if (error instanceof DOMException && error.name === "TimeoutError") {
      return { kind: "request_failed", message: "request timed out" }
    }
    if (error instanceof DOMException && error.name === "AbortError") {
      return { kind: "request_failed", message: "request was cancelled" }
    }
    if (error instanceof Error) {
      return { kind: "request_failed", message: sanitizeUploadFailureMessage(error.message) }
    }
    return { kind: "request_failed", message: "network request failed" }
  }
}

function buildUploadKey(uploadDir: string, fileName: string): string {
  return `${uploadDir.replace(/\/+$/, "")}/${fileName}`
}

function isSafeUploadFileName(fileName: string): boolean {
  return fileName !== "" && fileName !== "." && fileName !== ".."
}

function sanitizeUploadFailureMessage(message: string): string {
  return sanitizeText(message).replace(/https?:\/\/\S+/gi, "[redacted]")
}

function parseUploadPolicyResponse(responseText: string): UploadPolicyResult {
  try {
    const raw = JSON.parse(responseText)
    const parsed = UploadPolicyResponseSchema.safeParse(raw)
    if (parsed.success) {
      return { kind: "ready", policy: parsed.data.data }
    }
    const uploadHost = extractUploadHost(raw)
    return uploadHost && !uploadHost.startsWith("https://")
      ? { kind: "invalid_upload_host", message: "DashScope upload_host must be an HTTPS URL" }
      : { kind: "invalid_policy", message: "DashScope upload policy response was missing required fields" }
  } catch {
    return { kind: "invalid_json", message: "DashScope upload policy response was not valid JSON" }
  }
}

function extractUploadHost(raw: unknown): string | undefined {
  if (typeof raw !== "object" || raw === null) return undefined
  const data = (raw as { readonly data?: { readonly upload_host?: unknown } }).data
  return typeof data?.upload_host === "string" ? data.upload_host : undefined
}

export async function probeVideo(input: ProbeInput): Promise<ProbeResult> {
  const video = await prepareWorkspaceVideo(input)
  if (video.kind !== "ready") return video

  const providerGate = ensureDashScopeProvider(input.endpoint)
  if (providerGate.kind !== "ready") return providerGate

  // Shared across all 3 stages so the 300s budget is not reset per stage.
  const signal = requestSignal(input.signal, input.timeoutMs ?? REQUEST_TIMEOUT_MS)
  const fetchImpl = input.fetchImpl
  const now = input.now ?? monotonicNow

  const start = now()
  const policyResult = await getUploadPolicy({
    model: input.model,
    ...(input.headers ? { headers: input.headers } : {}),
    signal,
    ...(fetchImpl ? { fetchImpl } : {}),
  })
  const afterPolicy = now()
  const policyMapped = mapPolicyFailure(policyResult)
  if (policyMapped) return policyMapped
  if (policyResult.kind !== "ready") return { kind: "policy_unavailable", message: "DashScope upload policy was unavailable" }

  const limitBytes = policyResult.policy.max_file_size_mb * 1024 * 1024
  if (video.sizeBytes > limitBytes) {
    return {
      kind: "payload_too_large",
      message: `MP4 exceeds the provider ${policyResult.policy.max_file_size_mb} MiB upload limit`,
    }
  }

  const uploadResult = await uploadToOss({
    policy: policyResult.policy,
    absolutePath: video.absolutePath,
    signal,
    ...(fetchImpl ? { fetchImpl } : {}),
  })
  const afterUpload = now()
  const uploadMapped = mapUploadFailure(uploadResult)
  if (uploadMapped) return uploadMapped
  if (uploadResult.kind !== "ready") return { kind: "upload_failed", message: "OSS upload did not complete" }

  try {
    const response = await (fetchImpl ?? fetch)(buildChatCompletionsEndpoint(input.endpoint), {
      method: "POST",
      headers: buildChatHeaders(input.headers),
      body: JSON.stringify(
        buildVideoPayload({
          model: input.model,
          prompt: input.prompt,
          videoUrl: uploadResult.ossUrl,
          fps: input.fps,
        }),
      ),
      signal,
    })
    const responseText = await response.text()
    const body = parseResponseBody(responseText)
    const afterInference = now()
    const timings: ProbeTimings = {
      policyMs: afterPolicy - start,
      uploadMs: afterUpload - afterPolicy,
      inferenceMs: afterInference - afterUpload,
      totalMs: afterInference - start,
    }

    const classified = classifyProviderResponse({ status: response.status, body })
    if (classified.kind !== "success") return classified

    return {
      ...classified,
      timings,
      ...(input.debug
        ? { debug: `HTTP ${response.status}\n${sanitizeText(responseText).slice(0, 2000)}` }
        : {}),
    }
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

function ensureDashScopeProvider(
  endpoint: string,
): { readonly kind: "ready" } | { readonly kind: "unsupported_provider"; readonly message: string } {
  try {
    if (new URL(endpoint).host === DASHSCOPE_HOST) return { kind: "ready" }
  } catch {
    return { kind: "unsupported_provider", message: "video_probe requires a DashScope endpoint" }
  }
  return {
    kind: "unsupported_provider",
    message: "video_probe only supports the DashScope (Qianwen) OSS upload path",
  }
}

function mapPolicyFailure(
  result: UploadPolicyResult,
): { readonly kind: "policy_unavailable" | "request_failed"; readonly message: string } | undefined {
  switch (result.kind) {
    case "ready":
      return undefined
    case "http_rejection":
      return { kind: "policy_unavailable", message: `DashScope rejected the upload policy request (HTTP ${result.status})` }
    case "invalid_json":
    case "invalid_policy":
    case "invalid_upload_host":
      return { kind: "policy_unavailable", message: result.message }
    case "request_failed":
      return isCancellationMessage(result.message)
        ? { kind: "request_failed", message: result.message }
        : { kind: "policy_unavailable", message: "DashScope upload policy request failed" }
    default:
      return assertNever(result)
  }
}

function mapUploadFailure(
  result: OssUploadResult,
): { readonly kind: "upload_failed" | "request_failed"; readonly message: string } | undefined {
  switch (result.kind) {
    case "ready":
      return undefined
    case "http_rejection":
      return { kind: "upload_failed", message: `OSS upload rejected the request (HTTP ${result.status})` }
    case "invalid_file_name":
      return { kind: "upload_failed", message: result.message }
    case "request_failed":
      return isCancellationMessage(result.message)
        ? { kind: "request_failed", message: result.message }
        : { kind: "upload_failed", message: "OSS upload request failed" }
    default:
      return assertNever(result)
  }
}

function isCancellationMessage(message: string): boolean {
  return message === "request timed out" || message === "request was cancelled"
}

function assertNever(value: never): never {
  throw new Error(`unexpected variant: ${String(value)}`)
}

function requestSignal(signal: AbortSignal | undefined, timeoutMs: number): AbortSignal {
  const timeout = AbortSignal.timeout(timeoutMs)
  return signal ? AbortSignal.any([signal, timeout]) : timeout
}

function monotonicNow(): number {
  return performance.now()
}

export function formatProbeDiagnostics(result: ProbeResult): string {
  switch (result.kind) {
    case "success":
      return formatSuccess(result)
    case "http_rejection":
      return `[video_probe:http_rejection] HTTP ${result.status}: ${result.message}`
    case "invalid_response":
    case "invalid_path":
    case "invalid_video":
    case "unsupported_provider":
    case "policy_unavailable":
    case "upload_failed":
    case "request_failed":
      return `[video_probe:${result.kind}] ${result.message}`
    case "payload_too_large":
      return "status" in result
        ? `[video_probe:payload_too_large] HTTP 413: ${result.message}`
        : `[video_probe:payload_too_large] ${result.message}`
    default:
      return assertNeverFormat(result)
  }
}

function formatSuccess(result: Extract<ProbeResult, { readonly kind: "success" }>): string {
  const { diagnostics } = result
  const lines = [
    "video_probe succeeded. Assess chronological correctness; HTTP 200 alone does not prove video understanding.",
    `model: ${diagnostics.model ?? "unavailable"}`,
    `video_tokens: ${diagnostics.usage.videoTokens ?? "unavailable"}`,
    formatTokenTotals(diagnostics.usage),
    formatTimings(result.timings),
  ]
  if (result.debug) lines.push(`debug: ${result.debug}`)
  lines.push(`model response:\n${diagnostics.content}`)
  return lines.join("\n")
}

function formatTokenTotals(usage: ProbeUsage): string {
  const prompt = usage.promptTokens ?? "unavailable"
  const completion = usage.completionTokens ?? "unavailable"
  const total = usage.totalTokens ?? "unavailable"
  return `tokens: prompt=${prompt} completion=${completion} total=${total}`
}

function formatTimings(timings: ProbeTimings | undefined): string {
  if (!timings) return "timings: unavailable"
  return `timings(ms): policy=${Math.round(timings.policyMs)} upload=${Math.round(timings.uploadMs)} inference=${Math.round(timings.inferenceMs)} total=${Math.round(timings.totalMs)}`
}

function assertNeverFormat(value: never): string {
  return `Unexpected video probe result: ${String(value)}`
}
