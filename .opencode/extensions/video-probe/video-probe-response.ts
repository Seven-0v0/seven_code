import { z } from "zod"

export type ProbeUsage = {
  readonly videoTokens?: number
  readonly promptTokens?: number
  readonly completionTokens?: number
  readonly totalTokens?: number
}

export type ProbeTimings = {
  readonly policyMs: number
  readonly uploadMs: number
  readonly inferenceMs: number
  readonly totalMs: number
}

export type ProbeDiagnostics = {
  readonly content: string
  readonly usage: ProbeUsage
  readonly model?: string
}

export type ProviderResponse =
  | {
      readonly kind: "success"
      readonly text: string
      readonly diagnostics: ProbeDiagnostics
      readonly timings?: ProbeTimings
      readonly debug?: string
    }
  | { readonly kind: "http_rejection"; readonly status: number; readonly message: string }
  | { readonly kind: "payload_too_large"; readonly status?: 413; readonly message: string }
  | { readonly kind: "invalid_response"; readonly message: string }

const ProviderErrorSchema = z
  .object({
    error: z.object({ message: z.string() }).passthrough().optional(),
    message: z.string().optional(),
  })
  .passthrough()

const VideoTokensDetailSchema = z
  .object({ video_tokens: z.number().optional() })
  .passthrough()

const UsageSchema = z
  .object({
    prompt_tokens: z.number().optional(),
    completion_tokens: z.number().optional(),
    total_tokens: z.number().optional(),
    video_tokens: z.number().optional(),
    prompt_tokens_details: VideoTokensDetailSchema.optional(),
    input_tokens_details: VideoTokensDetailSchema.optional(),
  })
  .passthrough()

const ProviderSuccessSchema = z
  .object({
    choices: z
      .array(
        z
          .object({
            message: z.object({ content: z.string() }).passthrough().optional(),
            text: z.string().optional(),
          })
          .passthrough(),
      )
      .optional(),
    output_text: z.string().optional(),
    text: z.string().optional(),
    model: z.string().optional(),
    usage: UsageSchema.optional(),
  })
  .passthrough()

export function classifyProviderResponse(input: {
  readonly status: number
  readonly body: unknown
}): ProviderResponse {
  const message = extractProviderMessage(input.body)
  if (input.status === 413) {
    return {
      kind: "payload_too_large",
      status: 413,
      message: message ?? "provider rejected the payload as too large",
    }
  }
  if (input.status < 200 || input.status >= 300) {
    return { kind: "http_rejection", status: input.status, message: message ?? "provider rejected the request" }
  }

  const diagnostics = extractDiagnostics(input.body)
  return diagnostics
    ? { kind: "success", text: diagnostics.content, diagnostics }
    : { kind: "invalid_response", message: "provider response did not contain assistant text" }
}

export function parseResponseBody(text: string): unknown {
  if (!text) return {}
  
  // 检查是否是 SSE 流式响应
  if (text.startsWith("data: ")) {
    return parseSSEStream(text)
  }
  
  try {
    return JSON.parse(text)
  } catch (error) {
    return error instanceof SyntaxError ? { message: sanitizeText(text) } : {}
  }
}

function parseSSEStream(text: string): unknown {
  const lines = text.split("\n")
  let fullContent = ""
  let fullReasoning = ""
  
  for (const line of lines) {
    if (line.startsWith("data: ")) {
      const jsonStr = line.slice(6).trim()
      if (!jsonStr || jsonStr === "[DONE]") continue
      
      try {
        const chunk = JSON.parse(jsonStr)
        const delta = chunk.choices?.[0]?.delta
        if (delta?.content) {
          fullContent += delta.content
        }
        if (delta?.reasoning_content) {
          fullReasoning += delta.reasoning_content
        }
      } catch {
        // 忽略解析错误
      }
    }
  }
  
  // 返回类似 OpenAI 格式的响应
  return {
    choices: [
      {
        message: {
          content: fullContent,
          reasoning_content: fullReasoning,
        },
      },
    ],
  }
}

export function sanitizeText(value: string): string {
  return value
    .replace(
      /((?:api[_-]?key|authorization)\s*[:=]\s*)(?:bearer\s+)?[^\s"',}]+/gi,
      "$1[redacted]",
    )
    .replace(/(bearer\s+)[^\s"',}]+/gi, "$1[redacted]")
    .replace(/\bsk-[A-Za-z0-9_-]{8,}\b/g, "[redacted]")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 1_000)
}

function extractDiagnostics(body: unknown): ProbeDiagnostics | undefined {
  const parsed = ProviderSuccessSchema.safeParse(body)
  if (!parsed.success) return undefined
  const content = extractAssistantText(parsed.data)
  if (!content) return undefined
  const usage = extractUsage(parsed.data.usage)
  return parsed.data.model?.trim()
    ? { content, usage, model: parsed.data.model }
    : { content, usage }
}

type ParsedSuccess = z.infer<typeof ProviderSuccessSchema>

function extractAssistantText(data: ParsedSuccess): string | undefined {
  if (data.output_text?.trim()) return data.output_text
  if (data.text?.trim()) return data.text
  const first = data.choices?.[0]
  if (!first) return undefined
  if (first.text?.trim()) return first.text

  const message = first.message as { content?: string; reasoning_content?: string } | undefined
  if (message?.content?.trim()) return message.content
  if (message?.reasoning_content?.trim()) return message.reasoning_content

  return undefined
}

function extractUsage(usage: z.infer<typeof UsageSchema> | undefined): ProbeUsage {
  if (!usage) return {}
  const videoTokens =
    usage.prompt_tokens_details?.video_tokens ??
    usage.input_tokens_details?.video_tokens ??
    usage.video_tokens
  const result: {
    videoTokens?: number
    promptTokens?: number
    completionTokens?: number
    totalTokens?: number
  } = {}
  if (videoTokens !== undefined) result.videoTokens = videoTokens
  if (usage.prompt_tokens !== undefined) result.promptTokens = usage.prompt_tokens
  if (usage.completion_tokens !== undefined) result.completionTokens = usage.completion_tokens
  if (usage.total_tokens !== undefined) result.totalTokens = usage.total_tokens
  return result
}

function extractProviderMessage(body: unknown): string | undefined {
  const parsed = ProviderErrorSchema.safeParse(body)
  if (!parsed.success) return undefined
  return parsed.data.message
    ? sanitizeText(parsed.data.message)
    : parsed.data.error?.message
      ? sanitizeText(parsed.data.error.message)
      : undefined
}
