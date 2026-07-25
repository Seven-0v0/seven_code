import assert from "node:assert/strict"
import { mkdtemp, writeFile, rm } from "node:fs/promises"
import { tmpdir } from "node:os"
import { join } from "node:path"
import { after, before, describe, it } from "node:test"

import {
  DEFAULT_FPS,
  FpsSchema,
  MAXIMUM_FPS,
  MINIMUM_FPS,
  REQUEST_TIMEOUT_MS,
  TimeoutMsSchema,
  buildVideoPayload,
  formatProbeDiagnostics,
  getUploadPolicy,
  probeVideo,
  uploadToOss,
  type ProbeResult,
} from "./video-probe-core.js"

type UploadPolicyData = { policy: string; signature: string; upload_dir: string; upload_host: string; oss_access_key_id: string; x_oss_object_acl: string; x_oss_forbid_overwrite: string; max_file_size_mb: number | string; expire_in_seconds: number | string }

type VideoBlock = {
  type: "video_url"
  video_url: { url: string; fps?: unknown }
  fps: number
}

type MockResponse = { readonly status: number; readonly body: string }

type FetchLike = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>

type UploadField = { readonly name: string; readonly value: string | Blob }

function extractVideoBlock(payload: unknown): VideoBlock {
  const message = (payload as { messages: readonly { content: readonly unknown[] }[] }).messages[0]
  const videoBlock = message?.content.find(
    (block): block is VideoBlock =>
      typeof block === "object" && block !== null && (block as { type?: string }).type === "video_url",
  )
  if (!videoBlock) throw new Error("no video_url block in payload")
  return videoBlock
}

function makeUploadFetch(
  response: MockResponse,
  capture?: { url?: string; headers?: Record<string, string>; method?: string },
): FetchLike {
  return async (input: RequestInfo | URL, init?: RequestInit) => {
    if (capture) {
      capture.url = String(input)
      capture.method = init?.method ?? "GET"
      capture.headers = init?.headers ? Object.fromEntries(new Headers(init.headers).entries()) : {}
    }
    return new Response(response.body, { status: response.status })
  }
}

function captureFormData(body: BodyInit | null | undefined): UploadField[] {
  if (!(body instanceof FormData)) throw new Error("expected FormData body")
  return Array.from(body.entries()).map(([name, value]) => ({ name, value }))
}

describe("constants", () => {
  it("pins REQUEST_TIMEOUT_MS to 300 seconds", () => {
    assert.equal(REQUEST_TIMEOUT_MS, 300_000)
  })
})

describe("FpsSchema", () => {
  it("defaults to 2.0 when omitted", () => {
    assert.equal(FpsSchema.parse(undefined), DEFAULT_FPS)
    assert.equal(DEFAULT_FPS, 2.0)
  })

  it("accepts the inclusive lower and upper bounds", () => {
    assert.equal(FpsSchema.parse(MINIMUM_FPS), 0.1)
    assert.equal(FpsSchema.parse(MAXIMUM_FPS), 10)
  })

  it("rejects values below 0.1", () => {
    assert.equal(FpsSchema.safeParse(0.05).success, false)
  })

  it("rejects values above 10", () => {
    assert.equal(FpsSchema.safeParse(10.1).success, false)
  })
})

describe("TimeoutMsSchema", () => {
  it("accepts a positive integer", () => {
    assert.equal(TimeoutMsSchema.parse(120_000), 120_000)
  })

  it("rejects zero", () => {
    assert.equal(TimeoutMsSchema.safeParse(0).success, false)
  })

  it("rejects negatives", () => {
    assert.equal(TimeoutMsSchema.safeParse(-1).success, false)
  })

  it("rejects non-integers", () => {
    assert.equal(TimeoutMsSchema.safeParse(1.5).success, false)
  })
})

describe("buildVideoPayload", () => {
  it("places fps on the same video_url object as the oss url", () => {
    const payload = buildVideoPayload({
      model: "qianwen/qwen3.7-plus",
      prompt: "hello",
      videoUrl: "oss://uploads/clip.mp4",
      fps: 3.5,
    })
    const block = extractVideoBlock(payload)
    assert.equal(block.video_url.url, "oss://uploads/clip.mp4")
    assert.equal(block.fps, 3.5)
    assert.equal(block.video_url.fps, undefined)
    assert.deepEqual(Object.keys(block.video_url), ["url"])
    assert.equal(payload.model, "qianwen/qwen3.7-plus")
    assert.equal(payload.messages[0].content.length, 2)
    assert.ok(!JSON.stringify(payload).includes("data:"))
    assert.ok(!JSON.stringify(payload).includes("base64"))
  })
})

describe("getUploadPolicy", () => {
  it("returns a parsed policy on success", async () => {
    const capture: { url?: string; method?: string; headers?: Record<string, string> } = {}
    const fetchImpl = makeUploadFetch({ status: 200, body: JSON.stringify({
      request_id: "req-1",
      data: {
        policy: "policy",
        signature: "signature",
        upload_dir: "uploads/",
        upload_host: "https://oss.example.com",
        oss_access_key_id: "akid",
        x_oss_object_acl: "private",
        x_oss_forbid_overwrite: "true",
        max_file_size_mb: "1024",
        expire_in_seconds: 900,
      },
    } satisfies { request_id: string; data: UploadPolicyData }) }, capture)

    const result = await getUploadPolicy({ model: "qwen3.7-plus", headers: { authorization: "Bearer secret" }, fetchImpl })

    assert.equal(result.kind, "ready")
    if (result.kind !== "ready") throw new Error("expected ready")
    assert.equal(result.policy.upload_host, "https://oss.example.com")
    assert.equal(result.policy.max_file_size_mb, 1024)
    assert.equal(result.policy.expire_in_seconds, 900)
    assert.equal(capture.method, "GET")
    assert.equal(capture.headers?.authorization, "Bearer secret")
    assert.equal(capture.url, "https://dashscope.aliyuncs.com/api/v1/uploads?action=getPolicy&model=qwen3.7-plus")
  })

  it("preserves encoded model characters in the request URL", async () => {
    const capture: { url?: string } = {}
    const fetchImpl = makeUploadFetch({ status: 200, body: JSON.stringify({
      request_id: "req-2",
      data: {
        policy: "policy",
        signature: "signature",
        upload_dir: "uploads/",
        upload_host: "https://oss.example.com",
        oss_access_key_id: "akid",
        x_oss_object_acl: "private",
        x_oss_forbid_overwrite: "true",
        max_file_size_mb: 1,
        expire_in_seconds: 1,
      },
    } satisfies { request_id: string; data: UploadPolicyData }) }, capture)

    const result = await getUploadPolicy({ model: "qwen 3.7+/preview", fetchImpl })

    assert.equal(result.kind, "ready")
    assert.equal(capture.url, "https://dashscope.aliyuncs.com/api/v1/uploads?action=getPolicy&model=qwen+3.7%2B%2Fpreview")
  })

  it("returns http_rejection on non-2xx", async () => {
    const fetchImpl = makeUploadFetch({ status: 403, body: "authorization: Bearer secret denied" })

    const result = await getUploadPolicy({ model: "qwen3.7-plus", fetchImpl })

    assert.equal(result.kind, "http_rejection")
    if (result.kind !== "http_rejection") throw new Error("expected http_rejection")
    assert.equal(result.status, 403)
    assert.equal(result.message, "authorization: [redacted] denied")
  })

  it("returns invalid_json when the body is not JSON", async () => {
    const fetchImpl = makeUploadFetch({ status: 200, body: "not json" })

    const result = await getUploadPolicy({ model: "qwen3.7-plus", fetchImpl })

    assert.equal(result.kind, "invalid_json")
  })

  it("returns invalid_policy when required fields are missing", async () => {
    const fetchImpl = makeUploadFetch({ status: 200, body: JSON.stringify({ request_id: "req-3", data: { policy: "policy" } }) })

    const result = await getUploadPolicy({ model: "qwen3.7-plus", fetchImpl })

    assert.equal(result.kind, "invalid_policy")
  })

  it("returns invalid_upload_host when the host is not https", async () => {
    const fetchImpl = makeUploadFetch({
      status: 200,
      body: JSON.stringify({
        request_id: "req-4",
        data: {
          policy: "policy",
          signature: "signature",
          upload_dir: "uploads/",
          upload_host: "http://oss.example.com",
          oss_access_key_id: "akid",
          x_oss_object_acl: "private",
          x_oss_forbid_overwrite: "true",
          max_file_size_mb: 1024,
          expire_in_seconds: 900,
        },
      }),
    })

    const result = await getUploadPolicy({ model: "qwen3.7-plus", fetchImpl })

    assert.equal(result.kind, "invalid_upload_host")
  })

  it("parses numeric strings for size and expiry", async () => {
    const fetchImpl = makeUploadFetch({
      status: 200,
      body: JSON.stringify({
        request_id: "req-5",
        data: {
          policy: "policy",
          signature: "signature",
          upload_dir: "uploads/",
          upload_host: "https://oss.example.com",
          oss_access_key_id: "akid",
          x_oss_object_acl: "private",
          x_oss_forbid_overwrite: "true",
          max_file_size_mb: "1024",
          expire_in_seconds: "900",
        },
      } satisfies { request_id: string; data: UploadPolicyData }),
    })

    const result = await getUploadPolicy({ model: "qwen3.7-plus", fetchImpl })

    assert.equal(result.kind, "ready")
    if (result.kind !== "ready") throw new Error("expected ready")
    assert.equal(result.policy.max_file_size_mb, 1024)
    assert.equal(result.policy.expire_in_seconds, 900)
  })
})

type ProbeStage = "policy" | "upload" | "chat"

type ProbeCapture = {
  stage: ProbeStage
  url: string
  method: string
  headers: Record<string, string>
  jsonBody?: unknown
  formFields?: UploadField[]
  signal: AbortSignal | undefined
}

type StageStubConfig = {
  policy?: MockResponse
  upload?: MockResponse
  chat?: MockResponse
}

function policyResponseBody(overrides?: Partial<UploadPolicyData>): string {
  const data: UploadPolicyData = {
    policy: "policy-token",
    signature: "signature-token",
    upload_dir: "uploads/",
    upload_host: "https://oss.example.com",
    oss_access_key_id: "akid-token",
    x_oss_object_acl: "private",
    x_oss_forbid_overwrite: "true",
    max_file_size_mb: 1024,
    expire_in_seconds: 900,
    ...overrides,
  }
  return JSON.stringify({ request_id: "req-probe", data })
}

function makeStageStub(config: StageStubConfig): { fetchImpl: FetchLike; captured: ProbeCapture[] } {
  const captured: ProbeCapture[] = []
  const fetchImpl: FetchLike = async (input, init) => {
    const url = String(input)
    const method = init?.method ?? "GET"
    const headers = init?.headers ? Object.fromEntries(new Headers(init.headers).entries()) : {}
    const signal = init?.signal ?? undefined
    if (url.includes("action=getPolicy")) {
      captured.push({ stage: "policy", url, method, headers, signal })
      const response = config.policy ?? { status: 200, body: policyResponseBody() }
      return new Response(response.body, { status: response.status })
    }
    if (init?.body instanceof FormData) {
      captured.push({ stage: "upload", url, method, headers, formFields: captureFormData(init.body), signal })
      const response = config.upload ?? { status: 200, body: "" }
      return new Response(response.body || null, { status: response.status })
    }
    const jsonBody = typeof init?.body === "string" ? JSON.parse(init.body) : undefined
    captured.push({ stage: "chat", url, method, headers, jsonBody, signal })
    const response = config.chat ?? {
      status: 200,
      body: JSON.stringify({ choices: [{ message: { content: "ok" } }] }),
    }
    return new Response(response.body, { status: response.status })
  }
  return { fetchImpl, captured }
}

describe("probeVideo", () => {
  const endpoint = "https://dashscope.aliyuncs.com/compatible-mode/v1"
  let workspace = ""
  const fileName = "clip.mp4"

  before(async () => {
    workspace = await mkdtemp(join(tmpdir(), "video-probe-test-"))
    await writeFile(join(workspace, fileName), Buffer.from([0, 1, 2, 3, 4, 5, 6, 7]))
    await writeFile(join(workspace, "big.mp4"), Buffer.alloc(2 * 1024 * 1024))
  })

  after(async () => {
    if (workspace) await rm(workspace, { recursive: true, force: true })
  })

  it("runs policy then upload then chat with a consistent model, oss url, and no base64", async () => {
    const { fetchImpl, captured } = makeStageStub({})

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      headers: { authorization: "Bearer secret" },
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: 4.0,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "success")
    assert.deepEqual(captured.map((request) => request.stage), ["policy", "upload", "chat"])

    const policyRequest = captured[0]
    const uploadRequest = captured[1]
    const chatRequest = captured[2]
    if (!policyRequest || !uploadRequest || !chatRequest) throw new Error("missing captured requests")

    assert.equal(policyRequest.method, "GET")
    assert.equal(new URL(policyRequest.url).searchParams.get("model"), "qwen3.7-plus")

    assert.equal(uploadRequest.method, "POST")
    assert.equal(uploadRequest.url, "https://oss.example.com")
    assert.ok(uploadRequest.formFields?.some((field) => field.value instanceof Blob))

    assert.equal(chatRequest.method, "POST")
    assert.equal(new URL(chatRequest.url).host, "dashscope.aliyuncs.com")
    assert.equal(chatRequest.headers["x-dashscope-ossresourceresolve"], "enable")

    const chatBody = chatRequest.jsonBody as { model: string }
    assert.equal(chatBody.model, "qwen3.7-plus")
    assert.equal(chatBody.model, new URL(policyRequest.url).searchParams.get("model"))

    const block = extractVideoBlock(chatRequest.jsonBody)
    assert.ok(block.video_url.url.startsWith("oss://"))
    assert.equal(block.fps, 4.0)
    assert.equal(block.video_url.fps, undefined)

    const serialized = JSON.stringify(chatRequest.jsonBody)
    assert.ok(serialized.includes("oss://"))
    assert.ok(!serialized.includes("data:"))
    assert.ok(!serialized.includes("base64"))
  })

  it("shares one AbortSignal across all three stages", async () => {
    const { fetchImpl, captured } = makeStageStub({})

    await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(captured.length, 3)
    assert.ok(captured[0]?.signal)
    assert.strictEqual(captured[0]?.signal, captured[1]?.signal)
    assert.strictEqual(captured[1]?.signal, captured[2]?.signal)
  })

  it("forces reserved chat headers even when input headers try to override them", async () => {
    const { fetchImpl, captured } = makeStageStub({})

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      headers: {
        authorization: "Bearer secret",
        "Content-Type": "text/plain",
        "x-dashscope-ossresourceresolve": "disable",
      },
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "success")
    const chatRequest = captured.find((request) => request.stage === "chat")
    if (!chatRequest) throw new Error("missing chat request")
    const finalHeaders = new Headers(chatRequest.headers)
    assert.equal(finalHeaders.get("content-type"), "application/json")
    assert.equal(finalHeaders.get("x-dashscope-ossresourceresolve"), "enable")
    assert.equal(finalHeaders.get("authorization"), "Bearer secret")
  })

  it("fails closed as unsupported_provider for a non-DashScope endpoint", async () => {
    const { fetchImpl, captured } = makeStageStub({})

    const result = await probeVideo({
      endpoint: "https://example.test/v1",
      filePath: fileName,
      headers: { authorization: "Bearer secret" },
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "unsupported_provider")
    assert.equal(captured.length, 0)
  })

  it("short-circuits on policy rejection without uploading or inferring", async () => {
    const { fetchImpl, captured } = makeStageStub({
      policy: { status: 403, body: "authorization: Bearer secret denied" },
    })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "policy_unavailable")
    assert.deepEqual(captured.map((request) => request.stage), ["policy"])
  })

  it("rejects a file larger than the policy limit before uploading or inferring", async () => {
    const { fetchImpl, captured } = makeStageStub({
      policy: { status: 200, body: policyResponseBody({ max_file_size_mb: 1 }) },
    })

    const result = await probeVideo({
      endpoint,
      filePath: "big.mp4",
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "payload_too_large")
    assert.deepEqual(captured.map((request) => request.stage), ["policy"])
  })

  it("maps an OSS upload rejection to upload_failed without inferring", async () => {
    const { fetchImpl, captured } = makeStageStub({
      upload: { status: 403, body: "denied https://oss.example.com/uploads/clip.mp4" },
    })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "upload_failed")
    assert.deepEqual(captured.map((request) => request.stage), ["policy", "upload"])
  })

  it("adds sanitized debug detail without dropping structured diagnostics when debug is enabled", async () => {
    const { fetchImpl } = makeStageStub({
      chat: {
        status: 200,
        body: JSON.stringify({
          model: "qwen3.7-plus",
          choices: [{ message: { content: "ok" } }],
          usage: { prompt_tokens: 10, completion_tokens: 4, total_tokens: 14, prompt_tokens_details: { video_tokens: 7 } },
        }),
      },
    })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      debug: true,
      fetchImpl,
    })

    assert.equal(result.kind, "success")
    if (result.kind !== "success") throw new Error("expected success")
    assert.equal(result.diagnostics.content, "ok")
    assert.equal(result.diagnostics.model, "qwen3.7-plus")
    assert.equal(result.diagnostics.usage.videoTokens, 7)
    assert.equal(result.diagnostics.usage.totalTokens, 14)
    assert.ok(result.debug?.includes("HTTP 200"))
    assert.ok(!result.debug?.includes("reasoning_content"))
  })

  it("extracts assistant text, model, video_tokens, and token totals from a confirmed Qwen success shape", async () => {
    const { fetchImpl } = makeStageStub({
      chat: {
        status: 200,
        body: JSON.stringify({
          model: "qwen3.7-plus",
          choices: [{ message: { content: "the sequence is 点赞 then V", reasoning_content: "long private chain" } }],
          usage: {
            prompt_tokens: 1234,
            completion_tokens: 56,
            total_tokens: 1290,
            prompt_tokens_details: { video_tokens: 1200 },
          },
        }),
      },
    })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "success")
    if (result.kind !== "success") throw new Error("expected success")
    assert.equal(result.diagnostics.content, "the sequence is 点赞 then V")
    assert.equal(result.diagnostics.model, "qwen3.7-plus")
    assert.equal(result.diagnostics.usage.videoTokens, 1200)
    assert.equal(result.diagnostics.usage.promptTokens, 1234)
    assert.equal(result.diagnostics.usage.completionTokens, 56)
    assert.equal(result.diagnostics.usage.totalTokens, 1290)
    assert.equal(result.debug, undefined)
    assert.ok(!JSON.stringify(result).includes("reasoning_content"))
    assert.ok(!JSON.stringify(result).includes("long private chain"))
  })

  it("keeps success with unavailable usage when the response omits the usage block", async () => {
    const { fetchImpl } = makeStageStub({
      chat: { status: 200, body: JSON.stringify({ model: "qwen3.7-plus", choices: [{ message: { content: "ok" } }] }) },
    })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "success")
    if (result.kind !== "success") throw new Error("expected success")
    assert.equal(result.diagnostics.usage.videoTokens, undefined)
    assert.equal(result.diagnostics.usage.totalTokens, undefined)
    assert.deepEqual(result.diagnostics.usage, {})
  })

  it("reads video_tokens from usage.input_tokens_details as an alternate nesting", async () => {
    const { fetchImpl } = makeStageStub({
      chat: {
        status: 200,
        body: JSON.stringify({
          choices: [{ message: { content: "ok" } }],
          usage: { input_tokens_details: { video_tokens: 42 }, total_tokens: 99 },
        }),
      },
    })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "success")
    if (result.kind !== "success") throw new Error("expected success")
    assert.equal(result.diagnostics.usage.videoTokens, 42)
    assert.equal(result.diagnostics.usage.totalTokens, 99)
    assert.equal(result.diagnostics.model, undefined)
  })

  it("reads video_tokens from a flat usage.video_tokens field", async () => {
    const { fetchImpl } = makeStageStub({
      chat: {
        status: 200,
        body: JSON.stringify({ choices: [{ message: { content: "ok" } }], usage: { video_tokens: 5 } }),
      },
    })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
    })

    assert.equal(result.kind, "success")
    if (result.kind !== "success") throw new Error("expected success")
    assert.equal(result.diagnostics.usage.videoTokens, 5)
  })

  it("measures policy, upload, inference, and total ms with an injected monotonic clock", async () => {
    const { fetchImpl } = makeStageStub({})
    const ticks = [1000, 1100, 1350, 1950]
    let index = 0
    const now = (): number => {
      const value = ticks[index] ?? ticks[ticks.length - 1] ?? 0
      index += 1
      return value
    }

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      fetchImpl,
      now,
    })

    assert.equal(result.kind, "success")
    if (result.kind !== "success") throw new Error("expected success")
    assert.deepEqual(result.timings, { policyMs: 100, uploadMs: 250, inferenceMs: 600, totalMs: 950 })
  })

  it("never reports success on a non-2xx chat response even when debug is enabled", async () => {
    const { fetchImpl } = makeStageStub({
      chat: { status: 400, body: JSON.stringify({ error: { message: "bad request" } }) },
    })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      workspace,
      debug: true,
      fetchImpl,
    })

    assert.notEqual(result.kind, "success")
    assert.equal(result.kind, "http_rejection")
  })

  it("propagates timeoutMs so a small window aborts the flow", async () => {
    const stalling: FetchLike = async (_url, init) =>
      new Promise<Response>((_resolve, reject) => {
        const keepEventLoopAlive = setTimeout(() => {}, 10_000)
        init?.signal?.addEventListener("abort", () => {
          clearTimeout(keepEventLoopAlive)
          reject(init.signal?.reason)
        })
      })

    const result = await probeVideo({
      endpoint,
      filePath: fileName,
      model: "qwen3.7-plus",
      prompt: "hi",
      fps: DEFAULT_FPS,
      timeoutMs: 5,
      workspace,
      fetchImpl: stalling,
    })

    assert.equal(result.kind, "request_failed")
    if (result.kind !== "request_failed") throw new Error("expected request_failed")
    assert.equal(result.message, "request timed out")
  })
})

describe("uploadToOss", () => {
  const policy = {
    policy: "policy-token",
    signature: "signature-token",
    upload_dir: "uploads///",
    upload_host: "https://oss.example.com",
    oss_access_key_id: "akid-token",
    x_oss_object_acl: "private",
    x_oss_forbid_overwrite: "true",
    max_file_size_mb: 1024,
    expire_in_seconds: 900,
  } satisfies UploadPolicyData

  let workspace = ""
  const fileName = "clip.mp4"

  before(async () => {
    workspace = await mkdtemp(join(tmpdir(), "video-probe-oss-test-"))
    await writeFile(join(workspace, fileName), Buffer.from([0, 1, 2, 3, 4, 5, 6, 7]))
  })

  after(async () => {
    if (workspace) await rm(workspace, { recursive: true, force: true })
  })

  it("posts multipart fields in the required order and returns oss url on success", async () => {
    let capturedUrl = ""
    let capturedMethod = ""
    let capturedSignalAborted = false
    let capturedFields: UploadField[] = []
    const fetchImpl: FetchLike = async (input, init) => {
      capturedUrl = String(input)
      capturedMethod = init?.method ?? ""
      capturedSignalAborted = init?.signal?.aborted ?? false
      capturedFields = captureFormData(init?.body)
      return new Response(null, { status: 200 })
    }

    const result = await uploadToOss({
      policy,
      absolutePath: join(workspace, fileName),
      fetchImpl,
    })

    assert.equal(result.kind, "ready")
    if (result.kind !== "ready") throw new Error("expected ready")
    assert.equal(result.key, "uploads/clip.mp4")
    assert.equal(result.ossUrl, "oss://uploads/clip.mp4")
    assert.equal(capturedUrl, policy.upload_host)
    assert.equal(capturedMethod, "POST")
    assert.equal(capturedSignalAborted, false)
    assert.deepEqual(capturedFields.map((field) => field.name), [
      "OSSAccessKeyId",
      "Signature",
      "policy",
      "x-oss-object-acl",
      "x-oss-forbid-overwrite",
      "key",
      "success_action_status",
      "file",
    ])
    assert.equal(capturedFields[0]?.value, policy.oss_access_key_id)
    assert.equal(capturedFields[1]?.value, policy.signature)
    assert.equal(capturedFields[2]?.value, policy.policy)
    assert.equal(capturedFields[3]?.value, policy.x_oss_object_acl)
    assert.equal(capturedFields[4]?.value, policy.x_oss_forbid_overwrite)
    assert.equal(capturedFields[5]?.value, "uploads/clip.mp4")
    assert.equal(capturedFields[6]?.value, "200")
    const fileField = capturedFields[7]?.value
    assert.ok(fileField instanceof Blob)
    assert.equal(fileField?.type, "video/mp4")
  })

  it("returns http_rejection with redacted failure details for non-2xx responses", async () => {
    const fetchImpl: FetchLike = async () =>
      new Response("authorization: Bearer secret https://oss.example.com/uploads/clip.mp4", { status: 403 })

    const result = await uploadToOss({
      policy,
      absolutePath: join(workspace, fileName),
      fetchImpl,
    })

    assert.equal(result.kind, "http_rejection")
    if (result.kind !== "http_rejection") throw new Error("expected http_rejection")
    assert.equal(result.status, 403)
    assert.equal(result.message, "authorization: [redacted] [redacted]")
  })

  it("returns request_failed when the request is cancelled", async () => {
    const controller = new AbortController()
    controller.abort()
    const fetchImpl: FetchLike = async () => {
      throw new DOMException("The operation was aborted.", "AbortError")
    }

    const result = await uploadToOss({
      policy,
      absolutePath: join(workspace, fileName),
      signal: controller.signal,
      fetchImpl,
    })

    assert.equal(result.kind, "request_failed")
    if (result.kind !== "request_failed") throw new Error("expected request_failed")
    assert.equal(result.message, "request was cancelled")
  })

  it("rejects invalid file names before uploading", async () => {
    const fetchImpl: FetchLike = async () => {
      throw new Error("should not be called")
    }

    const result = await uploadToOss({
      policy,
      absolutePath: `${workspace}/..`,
      fetchImpl,
    })

    assert.equal(result.kind, "invalid_file_name")
  })
})

describe("formatProbeDiagnostics", () => {
  it("renders model, video_tokens, token totals, timings, and content on a full success", () => {
    const result: ProbeResult = {
      kind: "success",
      text: "the sequence is 点赞 then V",
      diagnostics: {
        content: "the sequence is 点赞 then V",
        model: "qwen3.7-plus",
        usage: { videoTokens: 1200, promptTokens: 1234, completionTokens: 56, totalTokens: 1290 },
      },
      timings: { policyMs: 100, uploadMs: 250, inferenceMs: 600, totalMs: 950 },
    }

    const output = formatProbeDiagnostics(result)

    assert.ok(output.includes("model: qwen3.7-plus"))
    assert.ok(output.includes("video_tokens: 1200"))
    assert.ok(output.includes("tokens: prompt=1234 completion=56 total=1290"))
    assert.ok(output.includes("timings(ms): policy=100 upload=250 inference=600 total=950"))
    assert.ok(output.includes("the sequence is 点赞 then V"))
    assert.ok(!output.includes("reasoning_content"))
  })

  it("labels missing usage and model as unavailable without failing", () => {
    const result: ProbeResult = {
      kind: "success",
      text: "ok",
      diagnostics: { content: "ok", usage: {} },
      timings: { policyMs: 1, uploadMs: 2, inferenceMs: 3, totalMs: 6 },
    }

    const output = formatProbeDiagnostics(result)

    assert.ok(output.includes("model: unavailable"))
    assert.ok(output.includes("video_tokens: unavailable"))
    assert.ok(output.includes("tokens: prompt=unavailable completion=unavailable total=unavailable"))
  })

  it("reports timings as unavailable when absent", () => {
    const result: ProbeResult = {
      kind: "success",
      text: "ok",
      diagnostics: { content: "ok", usage: { videoTokens: 5 } },
    }

    const output = formatProbeDiagnostics(result)

    assert.ok(output.includes("timings: unavailable"))
    assert.ok(output.includes("video_tokens: 5"))
  })
})
