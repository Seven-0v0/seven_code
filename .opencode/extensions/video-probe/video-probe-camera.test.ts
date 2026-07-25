import assert from "node:assert/strict"
import { mkdtemp, rm, stat, writeFile } from "node:fs/promises"
import { tmpdir } from "node:os"
import { join } from "node:path"
import { after, before, describe, it } from "node:test"

import { z } from "zod"

import {
  CommandError,
  DEFAULT_CAPTURE_FPS,
  DEFAULT_DURATION_SECONDS,
  DEFAULT_HEIGHT,
  DEFAULT_WIDTH,
  MEASURED_FPS_WARNING_RATIO,
  RECORDER_TIMEOUT_MARGIN_MS,
  buildRecorderFailureMessage,
  cameraFpsWarning,
  captureCameraClip,
  formatCameraCaptureFailure,
  formatCameraProbe,
  parseRecorderOutput,
  type CommandResult,
  type RecorderMetadata,
  type RunCommand,
} from "./video-probe-camera.js"
import { createVideoProbeHooks } from "./video-probe.js"

const RECORDING: RecorderMetadata = {
  output_path: "/abs/out.mp4",
  camera_name: "UGREEN Camera",
  format_subtype: "420v",
  width: 1920,
  height: 1080,
  requested_fps: 30,
  active_fps: 30,
  requested_duration_seconds: 10,
  elapsed_seconds: 10.02,
  file_size_bytes: 5_000_000,
  codec: "h264",
  measured_fps: 29.9,
  measured_frame_count: 300,
  media_duration_seconds: 10.03,
}

function recorderStdout(overrides?: Partial<RecorderMetadata>): string {
  return JSON.stringify({ ...RECORDING, ...overrides })
}

describe("camera_video_probe tool registration", () => {
  const hooks = createVideoProbeHooks()

  it("registers camera_video_probe alongside the existing video_probe tool", () => {
    const tools = hooks.tool
    if (!tools) throw new Error("expected tool hooks")
    assert.ok("video_probe" in tools, "video_probe must remain registered")
    assert.ok("camera_video_probe" in tools, "camera_video_probe must be registered")
    const camera = tools.camera_video_probe
    if (!camera) throw new Error("expected camera_video_probe")
    assert.equal(typeof camera.execute, "function")
    assert.ok(camera.description.includes("camera"))
  })

  it("exposes exactly the required argument surface with correct defaults", () => {
    const tools = hooks.tool
    if (!tools) throw new Error("expected tool hooks")
    const camera = tools.camera_video_probe
    if (!camera) throw new Error("expected camera_video_probe")
    const args = camera.args
    assert.deepEqual(Object.keys(args).sort(), [
      "camera_name",
      "capture_fps",
      "debug",
      "duration_seconds",
      "fps",
      "height",
      "model",
      "prompt",
      "timeout_ms",
      "width",
    ])

    const parsed = z.object(args).parse({ camera_name: "UGREEN Camera", prompt: "describe" })
    assert.equal(parsed.duration_seconds, DEFAULT_DURATION_SECONDS)
    assert.equal(parsed.capture_fps, DEFAULT_CAPTURE_FPS)
    assert.equal(parsed.width, DEFAULT_WIDTH)
    assert.equal(parsed.height, DEFAULT_HEIGHT)
    assert.equal(parsed.fps, 2.0)
    assert.equal(parsed.model, undefined)
    assert.equal(parsed.timeout_ms, undefined)
    assert.equal(parsed.debug, undefined)
  })

  it("rejects a missing camera_name and non-positive numeric bounds", () => {
    const tools = hooks.tool
    if (!tools) throw new Error("expected tool hooks")
    const camera = tools.camera_video_probe
    if (!camera) throw new Error("expected camera_video_probe")
    const schema = z.object(camera.args)
    assert.equal(schema.safeParse({ prompt: "x" }).success, false)
    assert.equal(schema.safeParse({ camera_name: "c" }).success, false)
    assert.equal(schema.safeParse({ camera_name: "c", prompt: "x", duration_seconds: 0 }).success, false)
    assert.equal(schema.safeParse({ camera_name: "c", prompt: "x", width: 1.5 }).success, false)
    assert.equal(schema.safeParse({ camera_name: "c", prompt: "x", fps: 20 }).success, false)
  })
})

describe("parseRecorderOutput", () => {
  it("parses the last JSON line of recorder stdout", () => {
    const result = parseRecorderOutput(`[native-camera-recorder] warming up\n${recorderStdout()}\n`)
    assert.equal(result.kind, "ready")
    if (result.kind !== "ready") throw new Error("expected ready")
    assert.equal(result.recording.camera_name, "UGREEN Camera")
    assert.equal(result.recording.measured_fps, 29.9)
  })

  it("accepts a payload without the optional measured_fps block", () => {
    const result = parseRecorderOutput(
      recorderStdout({ measured_fps: undefined, measured_frame_count: undefined, media_duration_seconds: undefined }),
    )
    assert.equal(result.kind, "ready")
    if (result.kind !== "ready") throw new Error("expected ready")
    assert.equal(result.recording.measured_fps, undefined)
  })

  it("returns invalid_output on empty stdout", () => {
    assert.equal(parseRecorderOutput("   \n  ").kind, "invalid_output")
  })

  it("returns invalid_output on non-JSON stdout", () => {
    assert.equal(parseRecorderOutput("not json at all").kind, "invalid_output")
  })

  it("returns invalid_output when required fields are missing", () => {
    assert.equal(parseRecorderOutput(JSON.stringify({ output_path: "x" })).kind, "invalid_output")
  })
})

describe("cameraFpsWarning", () => {
  it("warns when measured_fps drops below the ratio threshold", () => {
    const warning = cameraFpsWarning({ ...RECORDING, measured_fps: 15 }, 30)
    assert.ok(warning)
    assert.ok(warning?.includes("warning"))
    assert.ok(warning?.includes("15"))
  })

  it("does not warn when measured_fps is close to capture_fps", () => {
    assert.equal(cameraFpsWarning({ ...RECORDING, measured_fps: 29 }, 30), undefined)
  })

  it("does not warn when measured_fps is unavailable", () => {
    assert.equal(cameraFpsWarning({ ...RECORDING, measured_fps: undefined }, 30), undefined)
  })

  it("pins the threshold at 70 percent", () => {
    assert.equal(MEASURED_FPS_WARNING_RATIO, 0.7)
    assert.equal(cameraFpsWarning({ ...RECORDING, measured_fps: 21 }, 30), undefined)
    assert.ok(cameraFpsWarning({ ...RECORDING, measured_fps: 20.9 }, 30))
  })
})

describe("buildRecorderFailureMessage", () => {
  it("reports a timeout when the process was killed", () => {
    const message = buildRecorderFailureMessage({ code: null, signal: "SIGTERM", killed: true, stdout: "", stderr: "", message: "" })
    assert.ok(message.includes("timed out"))
  })

  it("surfaces structured stderr error kind and message", () => {
    const message = buildRecorderFailureMessage({
      code: 3,
      signal: null,
      killed: false,
      stdout: "",
      stderr: '{"error":"camera_missing","message":"No device named X"}\n',
      message: "Command failed",
    })
    assert.ok(message.includes("code 3"))
    assert.ok(message.includes("camera_missing"))
    assert.ok(message.includes("No device named X"))
  })
})

describe("formatCameraProbe", () => {
  it("includes recording metadata, an fps warning, and probe diagnostics", () => {
    const output = formatCameraProbe({
      recording: { ...RECORDING, measured_fps: 10 },
      captureFps: 30,
      probe: {
        kind: "success",
        text: "the sequence is 点赞 then V",
        diagnostics: { content: "the sequence is 点赞 then V", model: "qwen3.7-plus", usage: { videoTokens: 1200 } },
        timings: { policyMs: 100, uploadMs: 250, inferenceMs: 600, totalMs: 950 },
      },
    })
    assert.ok(output.includes('camera="UGREEN Camera"'))
    assert.ok(output.includes("1920x1080"))
    assert.ok(output.includes("warning"))
    assert.ok(output.includes("model: qwen3.7-plus"))
    assert.ok(output.includes("the sequence is 点赞 then V"))
  })

  it("omits the warning when measured_fps is healthy", () => {
    const output = formatCameraProbe({
      recording: RECORDING,
      captureFps: 30,
      probe: {
        kind: "success",
        text: "ok",
        diagnostics: { content: "ok", usage: {} },
        timings: { policyMs: 1, uploadMs: 2, inferenceMs: 3, totalMs: 6 },
      },
    })
    assert.ok(!output.includes("warning"))
  })

  it("formats capture failures with the camera_video_probe prefix", () => {
    assert.equal(
      formatCameraCaptureFailure({ kind: "compile_failed", message: "swiftc broke" }),
      "[camera_video_probe:compile_failed] swiftc broke",
    )
  })
})

describe("captureCameraClip", () => {
  let extensionDir = ""
  let workspace = ""

  before(async () => {
    extensionDir = await mkdtemp(join(tmpdir(), "camera-ext-"))
    workspace = await mkdtemp(join(tmpdir(), "camera-ws-"))
    await writeFile(join(extensionDir, "native-camera-recorder.swift"), "// stub source")
  })

  after(async () => {
    if (extensionDir) await rm(extensionDir, { recursive: true, force: true })
    if (workspace) await rm(workspace, { recursive: true, force: true })
  })

  it("compiles once, runs the recorder, and returns parsed metadata with a workspace-relative path", async () => {
    const calls: { file: string; args: readonly string[]; timeoutMs?: number }[] = []
    const runCommand: RunCommand = async (file, args, options): Promise<CommandResult> => {
      calls.push({ file, args, ...(options.timeoutMs === undefined ? {} : { timeoutMs: options.timeoutMs }) })
      if (file === "swiftc") return { stdout: "", stderr: "" }
      return { stdout: recorderStdout({ output_path: args[args.length - 1] ?? "" }), stderr: "[native-camera-recorder] done" }
    }

    const outputAbsolutePath = join(workspace, ".opencode", ".tmp-video-capture", "clip.mp4")
    const result = await captureCameraClip({
      extensionDir,
      workspace,
      cameraName: "UGREEN Camera",
      durationSeconds: 10,
      width: 1920,
      height: 1080,
      captureFps: 30,
      outputAbsolutePath,
      runCommand,
    })

    assert.equal(result.kind, "ready")
    if (result.kind !== "ready") throw new Error("expected ready")
    assert.equal(result.workspaceRelativePath, join(".opencode", ".tmp-video-capture", "clip.mp4"))
    assert.equal(result.recording.camera_name, "UGREEN Camera")

    const swiftc = calls.find((call) => call.file === "swiftc")
    const recorder = calls.find((call) => call.file !== "swiftc")
    assert.ok(swiftc, "swiftc must be invoked when the binary is absent")
    assert.ok(recorder, "recorder binary must be invoked")
    assert.deepEqual(recorder?.args.slice(0, 2), ["--camera-name", "UGREEN Camera"])
    assert.ok(recorder?.args.includes("--duration-seconds"))
    assert.equal(recorder?.timeoutMs, 10 * 1000 + RECORDER_TIMEOUT_MARGIN_MS)
    // the .cache/ compile target lives under the extension dir
    await stat(join(extensionDir, ".cache"))
  })

  it("maps a recorder subprocess failure to recorder_failed with stderr detail", async () => {
    const runCommand: RunCommand = async (file): Promise<CommandResult> => {
      if (file === "swiftc") return { stdout: "", stderr: "" }
      throw new CommandError({
        code: 3,
        signal: null,
        killed: false,
        stdout: "",
        stderr: '{"error":"camera_missing","message":"No device"}',
        message: "Command failed",
      })
    }

    const result = await captureCameraClip({
      extensionDir,
      workspace,
      cameraName: "Ghost Camera",
      durationSeconds: 5,
      width: 1280,
      height: 720,
      captureFps: 30,
      outputAbsolutePath: join(workspace, ".opencode", ".tmp-video-capture", "fail.mp4"),
      runCommand,
    })

    assert.equal(result.kind, "recorder_failed")
    if (result.kind !== "recorder_failed") throw new Error("expected recorder_failed")
    assert.ok(result.message.includes("camera_missing"))
  })

  it("returns invalid_output when the recorder emits no JSON", async () => {
    const runCommand: RunCommand = async (file): Promise<CommandResult> => {
      if (file === "swiftc") return { stdout: "", stderr: "" }
      return { stdout: "warming up but never finished", stderr: "" }
    }

    const result = await captureCameraClip({
      extensionDir,
      workspace,
      cameraName: "UGREEN Camera",
      durationSeconds: 3,
      width: 640,
      height: 480,
      captureFps: 30,
      outputAbsolutePath: join(workspace, ".opencode", ".tmp-video-capture", "noout.mp4"),
      runCommand,
    })

    assert.equal(result.kind, "invalid_output")
  })
})
