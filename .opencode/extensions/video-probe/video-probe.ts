import { tool, type Hooks, type Plugin } from "@opencode-ai/plugin"
import { z } from "zod"

import {
  DEFAULT_PROMPT,
  MAXIMUM_VIDEO_BYTES,
  probeVideo,
  resolveModelSelection,
  type ModelReference,
  type ProbeResult,
} from "./video-probe-core"

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

export function formatProbeDiagnostics(result: ProbeResult): string {
  switch (result.kind) {
    case "success":
      return `video_probe succeeded. Assess chronological correctness; HTTP 200 alone does not prove video understanding. Model response:\n${result.text}`
    case "http_rejection":
      return `[video_probe:http_rejection] HTTP ${result.status}: ${result.message}`
    case "invalid_response":
    case "invalid_path":
    case "invalid_video":
    case "request_failed":
      return `[video_probe:${result.kind}] ${result.message}`
    case "payload_too_large":
      return "status" in result
        ? `[video_probe:payload_too_large] HTTP 413: ${result.message}`
        : `[video_probe:payload_too_large] ${result.message}`
    default:
      return assertNever(result)
  }
}

function assertNever(value: never): string {
  return `Unexpected video probe result: ${String(value)}`
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
          "Directly send a workspace MP4 as an OpenAI-compatible video_url data URI to the active model. This bypasses OpenCode file serialization.",
        args: {
          file_path: tool.schema.string().min(1).describe("Workspace-relative path to an MP4 file"),
          prompt: tool.schema.string().min(1).optional().describe("Analysis request for the video"),
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
              maximumBytes: MAXIMUM_VIDEO_BYTES,
              model: selection.modelID,
              prompt: args.prompt ?? DEFAULT_PROMPT,
              signal: context.abort,
              workspace: context.directory,
              worktree: context.worktree,
              ...(args.debug === undefined ? {} : { debug: args.debug }),
            }),
          )
        },
      }),
    },
  }
}

export const VideoProbePlugin: Plugin = async () => createVideoProbeHooks()

export default VideoProbePlugin
