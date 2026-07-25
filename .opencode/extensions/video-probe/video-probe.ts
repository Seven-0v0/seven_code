import { randomUUID } from "node:crypto"
import { rm } from "node:fs/promises"
import { join } from "node:path"

import { tool, type Hooks, type Plugin } from "@opencode-ai/plugin"
import { z } from "zod"

import {
  DEFAULT_PROMPT,
  FpsSchema,
  TimeoutMsSchema,
  formatProbeDiagnostics,
  probeVideo,
  resolveModelSelection,
  type ModelReference,
} from "./video-probe-core"
import {
  CaptureFpsSchema,
  DurationSecondsSchema,
  EXTENSION_DIR,
  HeightSchema,
  WidthSchema,
  captureCameraClip,
  formatCameraCaptureFailure,
  formatCameraProbe,
} from "./video-probe-camera"

export { formatProbeDiagnostics } from "./video-probe-core"

type RuntimeProvider = {
  readonly options: {
    readonly apiKey: string | undefined
    readonly baseURL: string | undefined
    readonly headers: Readonly<Record<string, string>>
  }
  readonly models: Readonly<Record<string, { readonly headers: Readonly<Record<string, string>> }>>
}

type RuntimeConfig = {
  readonly provider: Readonly<Record<string, RuntimeProvider>>
}

type ProviderRequest =
  | { readonly kind: "ready"; readonly endpoint: string; readonly headers: Readonly<Record<string, string>> }
  | { readonly kind: "missing_provider_configuration"; readonly message: string }

const HeadersSchema = z.record(z.string(), z.string())
const RuntimeConfigSchema = z
  .object({
    provider: z
      .record(
        z.string(),
        z
          .object({
            options: z
              .object({
                apiKey: z.string().trim().min(1).optional(),
                baseURL: z.string().trim().min(1).optional(),
                headers: HeadersSchema.optional(),
              })
              .passthrough()
              .optional(),
            models: z
              .record(
                z.string(),
                z
                  .object({
                    headers: HeadersSchema.optional(),
                    request: z.object({ headers: HeadersSchema.optional() }).passthrough().optional(),
                  })
                  .passthrough(),
              )
              .optional(),
          })
          .passthrough(),
      )
      .optional(),
  })
  .passthrough()

function parseRuntimeConfig(value: unknown): RuntimeConfig {
  const parsed = RuntimeConfigSchema.safeParse(value)
  if (!parsed.success || !parsed.data.provider) return { provider: {} }
  return {
    provider: Object.fromEntries(
      Object.entries(parsed.data.provider).map(([providerID, configured]) => {
        const options = configured.options
        return [
          providerID,
          {
            options: {
              apiKey: options?.apiKey,
              baseURL: options?.baseURL,
              headers: options?.headers ?? {},
            },
            models: Object.fromEntries(
              Object.entries(configured.models ?? {}).map(([modelID, model]) => [
                modelID,
                { headers: { ...model.headers, ...model.request?.headers } },
              ]),
            ),
          },
        ]
      }),
    ),
  }
}

function resolveProviderRequest(input: {
  readonly config: RuntimeConfig
  readonly providerID: string
  readonly modelID: string
}): ProviderRequest {
  const provider = input.config.provider[input.providerID]
  if (!provider?.options.baseURL) {
    return {
      kind: "missing_provider_configuration",
      message: `No OpenAI-compatible baseURL is configured for provider ${input.providerID}.`,
    }
  }

  const headers = { ...provider.options.headers, ...provider.models[input.modelID]?.headers }
  const authorizationHeader = Object.keys(headers).some(
    (name) => name.toLowerCase() === "authorization",
  )
  if (authorizationHeader) return { kind: "ready", endpoint: provider.options.baseURL, headers }
  if (provider.options.apiKey) {
    return {
      kind: "ready",
      endpoint: provider.options.baseURL,
      headers: { ...headers, authorization: `Bearer ${provider.options.apiKey}` },
    }
  }
  return {
    kind: "missing_provider_configuration",
    message: `No API key or authorization header is available for provider ${input.providerID}.`,
  }
}

export function createVideoProbeHooks(): Pick<Hooks, "config" | "chat.message" | "tool"> {
  const sessionModels = new Map<string, ModelReference>()
  let runtimeConfig: RuntimeConfig = { provider: {} }

  return {
    config: async (config) => {
      runtimeConfig = parseRuntimeConfig(config)
    },
    "chat.message": async (input) => {
      if (input.model) sessionModels.set(input.sessionID, input.model)
    },
    tool: {
      video_probe: tool({
        description:
          "Upload a workspace MP4 to DashScope OSS and send its oss:// URL to the active Qianwen model for video understanding. This bypasses OpenCode file serialization.",
        args: {
          file_path: tool.schema.string().min(1).describe("Workspace-relative path to an MP4 file"),
          prompt: tool.schema.string().min(1).optional().describe("Analysis request for the video"),
          model: tool.schema
            .string()
            .min(1)
            .optional()
            .describe("Optional modelID, or providerID/modelID before the session model is known"),
          fps: FpsSchema.describe("Frame sampling rate (0.1-10, default 2.0)"),
          timeout_ms: TimeoutMsSchema.optional().describe(
            "Optional per-request timeout in milliseconds (positive integer)",
          ),
          debug: tool.schema.boolean().optional().describe("Print raw response for debugging"),
        },
        async execute(args, context) {
          const selection = resolveModelSelection({
            observed: sessionModels.get(context.sessionID),
            override: args.model,
          })
          if (selection.kind !== "ready") return `[video_probe:${selection.kind}] ${selection.message}`

          const provider = resolveProviderRequest({
            config: runtimeConfig,
            providerID: selection.providerID,
            modelID: selection.modelID,
          })
          if (provider.kind !== "ready") return `[video_probe:${provider.kind}] ${provider.message}`

          return formatProbeDiagnostics(
            await probeVideo({
              endpoint: provider.endpoint,
              filePath: args.file_path,
              headers: provider.headers,
              model: selection.modelID,
              prompt: args.prompt ?? DEFAULT_PROMPT,
              fps: args.fps,
              signal: context.abort,
              workspace: context.directory,
              worktree: context.worktree,
              ...(args.timeout_ms === undefined ? {} : { timeoutMs: args.timeout_ms }),
              ...(args.debug === undefined ? {} : { debug: args.debug }),
            }),
          )
        },
      }),
      camera_video_probe: tool({
        description:
          "Record a clip from a named macOS camera, then upload it to DashScope OSS and send it to the active Qianwen model for video understanding. One call performs record → verify → upload → analyze → cleanup. Expect roughly 35-60s end to end.",
        args: {
          camera_name: tool.schema.string().min(1).describe("Exact localized camera device name (e.g. \"UGREEN Camera\")"),
          duration_seconds: DurationSecondsSchema.describe("Recording duration in seconds (positive, default 10)"),
          prompt: tool.schema.string().min(1).describe("Analysis request for the recorded video"),
          fps: FpsSchema.describe("Model frame sampling rate (0.1-10, default 2.0)"),
          width: WidthSchema.describe("Capture frame width in pixels (positive int, default 1920)"),
          height: HeightSchema.describe("Capture frame height in pixels (positive int, default 1080)"),
          capture_fps: CaptureFpsSchema.describe("Requested camera capture rate (positive, default 30)"),
          timeout_ms: TimeoutMsSchema.optional().describe(
            "Optional per-request timeout in milliseconds for the upload+inference round trip",
          ),
          model: tool.schema
            .string()
            .min(1)
            .optional()
            .describe("Optional modelID, or providerID/modelID before the session model is known"),
          debug: tool.schema.boolean().optional().describe("Print raw response for debugging"),
        },
        async execute(args, context) {
          const selection = resolveModelSelection({
            observed: sessionModels.get(context.sessionID),
            override: args.model,
          })
          if (selection.kind !== "ready") return `[camera_video_probe:${selection.kind}] ${selection.message}`

          const provider = resolveProviderRequest({
            config: runtimeConfig,
            providerID: selection.providerID,
            modelID: selection.modelID,
          })
          if (provider.kind !== "ready") return `[camera_video_probe:${provider.kind}] ${provider.message}`

          const outputAbsolutePath = join(context.directory, ".opencode", ".tmp-video-capture", `${randomUUID()}.mp4`)

          const capture = await captureCameraClip({
            extensionDir: EXTENSION_DIR,
            workspace: context.directory,
            cameraName: args.camera_name,
            durationSeconds: args.duration_seconds,
            width: args.width,
            height: args.height,
            captureFps: args.capture_fps,
            outputAbsolutePath,
            signal: context.abort,
          })
          if (capture.kind !== "ready") return formatCameraCaptureFailure(capture)

          try {
            const probe = await probeVideo({
              endpoint: provider.endpoint,
              filePath: capture.workspaceRelativePath,
              headers: provider.headers,
              model: selection.modelID,
              prompt: args.prompt,
              fps: args.fps,
              signal: context.abort,
              workspace: context.directory,
              worktree: context.worktree,
              ...(args.timeout_ms === undefined ? {} : { timeoutMs: args.timeout_ms }),
              ...(args.debug === undefined ? {} : { debug: args.debug }),
            })
            return formatCameraProbe({ recording: capture.recording, captureFps: args.capture_fps, probe })
          } finally {
            await rm(outputAbsolutePath, { force: true })
          }
        },
      }),
    },
  }
}

export const VideoProbePlugin: Plugin = async () => createVideoProbeHooks()

export default VideoProbePlugin
