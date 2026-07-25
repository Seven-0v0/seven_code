import { execFile } from "node:child_process"
import type { ExecFileOptionsWithStringEncoding } from "node:child_process"
import { mkdir, stat } from "node:fs/promises"
import { dirname, join, relative } from "node:path"
import { fileURLToPath } from "node:url"
import { promisify } from "node:util"

import { z } from "zod"

import { formatProbeDiagnostics, type ProbeResult } from "./video-probe-core.js"

export const DEFAULT_DURATION_SECONDS = 10
export const DEFAULT_WIDTH = 1920
export const DEFAULT_HEIGHT = 1080
export const DEFAULT_CAPTURE_FPS = 30

export const RECORDER_TIMEOUT_MARGIN_MS = 15_000
export const COMPILE_TIMEOUT_MS = 120_000
export const MEASURED_FPS_WARNING_RATIO = 0.7
const MAX_BUFFER_BYTES = 8 * 1024 * 1024

export const DurationSecondsSchema = z.number().positive().default(DEFAULT_DURATION_SECONDS)
export const CaptureFpsSchema = z.number().positive().default(DEFAULT_CAPTURE_FPS)
export const WidthSchema = z.number().int().positive().default(DEFAULT_WIDTH)
export const HeightSchema = z.number().int().positive().default(DEFAULT_HEIGHT)

/** Absolute directory holding this module and the sibling Swift recorder source. */
export const EXTENSION_DIR = dirname(fileURLToPath(import.meta.url))

const execFileAsync = promisify(execFile)

// Boundary-parsed mirror of the recorder's stdout JSON contract. Field names
// intentionally match the Swift wire format so parsing stays honest.
const RecorderOutputSchema = z.object({
  output_path: z.string().min(1),
  camera_name: z.string(),
  format_subtype: z.string(),
  width: z.number(),
  height: z.number(),
  requested_fps: z.number(),
  active_fps: z.number(),
  requested_duration_seconds: z.number(),
  elapsed_seconds: z.number(),
  file_size_bytes: z.number(),
  codec: z.string(),
  measured_fps: z.number().optional(),
  measured_frame_count: z.number().optional(),
  media_duration_seconds: z.number().optional(),
})

export type RecorderMetadata = z.infer<typeof RecorderOutputSchema>

export type CommandFailureDetail = {
  readonly code: number | null
  readonly signal: string | null
  readonly killed: boolean
  readonly stdout: string
  readonly stderr: string
  readonly message: string
}

export class CommandError extends Error {
  readonly detail: CommandFailureDetail
  constructor(detail: CommandFailureDetail) {
    super(detail.message)
    this.name = "CommandError"
    this.detail = detail
  }
}

export type CommandResult = { readonly stdout: string; readonly stderr: string }

export type RunCommand = (
  file: string,
  args: readonly string[],
  options: { readonly timeoutMs?: number; readonly signal?: AbortSignal; readonly cwd?: string },
) => Promise<CommandResult>

export type RecorderParseResult =
  | { readonly kind: "ready"; readonly recording: RecorderMetadata }
  | { readonly kind: "invalid_output"; readonly message: string; readonly stdout: string }

export type CameraCaptureResult =
  | {
      readonly kind: "ready"
      readonly recording: RecorderMetadata
      readonly outputAbsolutePath: string
      readonly workspaceRelativePath: string
      readonly stderr: string
    }
  | { readonly kind: "compile_failed"; readonly message: string }
  | { readonly kind: "recorder_failed"; readonly message: string; readonly stderr: string }
  | { readonly kind: "invalid_output"; readonly message: string; readonly stdout: string }

export type CameraCaptureInput = {
  readonly extensionDir: string
  readonly workspace: string
  readonly cameraName: string
  readonly durationSeconds: number
  readonly width: number
  readonly height: number
  readonly captureFps: number
  readonly outputAbsolutePath: string
  readonly signal?: AbortSignal
  readonly runCommand?: RunCommand
}

function normalizeExecError(error: unknown): CommandFailureDetail {
  const details = error as {
    code?: unknown
    signal?: unknown
    killed?: unknown
    stdout?: unknown
    stderr?: unknown
    message?: unknown
  }
  return {
    code: typeof details.code === "number" ? details.code : null,
    signal: typeof details.signal === "string" ? details.signal : null,
    killed: details.killed === true,
    stdout: typeof details.stdout === "string" ? details.stdout : "",
    stderr: typeof details.stderr === "string" ? details.stderr : "",
    message: typeof details.message === "string" ? details.message : "command failed",
  }
}

const defaultRunCommand: RunCommand = async (file, args, options) => {
  const execOptions: ExecFileOptionsWithStringEncoding = { encoding: "utf8", maxBuffer: MAX_BUFFER_BYTES }
  if (options.timeoutMs !== undefined) execOptions.timeout = options.timeoutMs
  if (options.signal) execOptions.signal = options.signal
  if (options.cwd) execOptions.cwd = options.cwd
  try {
    const { stdout, stderr } = await execFileAsync(file, [...args], execOptions)
    return { stdout, stderr }
  } catch (error) {
    throw new CommandError(normalizeExecError(error))
  }
}

function toCommandFailure(error: unknown): CommandFailureDetail {
  if (error instanceof CommandError) return error.detail
  if (error instanceof Error) return { code: null, signal: null, killed: false, stdout: "", stderr: "", message: error.message }
  return { code: null, signal: null, killed: false, stdout: "", stderr: "", message: "command failed" }
}

async function needsCompile(sourcePath: string, binaryPath: string): Promise<boolean> {
  try {
    const [source, binary] = await Promise.all([stat(sourcePath), stat(binaryPath)])
    return source.mtimeMs > binary.mtimeMs
  } catch {
    return true
  }
}

export function parseRecorderOutput(stdout: string): RecorderParseResult {
  const line = stdout
    .trim()
    .split("\n")
    .map((value) => value.trim())
    .filter((value) => value !== "")
    .at(-1)
  if (!line) return { kind: "invalid_output", message: "recorder produced no stdout JSON", stdout }
  let raw: unknown
  try {
    raw = JSON.parse(line)
  } catch {
    return { kind: "invalid_output", message: "recorder stdout was not valid JSON", stdout }
  }
  const parsed = RecorderOutputSchema.safeParse(raw)
  if (!parsed.success) return { kind: "invalid_output", message: "recorder stdout was missing required fields", stdout }
  return { kind: "ready", recording: parsed.data }
}

function parseRecorderStderr(stderr: string): { readonly kind?: string; readonly message?: string } {
  const line = stderr
    .trim()
    .split("\n")
    .map((value) => value.trim())
    .reverse()
    .find((value) => value.startsWith("{"))
  if (!line) return {}
  try {
    const raw: unknown = JSON.parse(line)
    if (typeof raw !== "object" || raw === null) return {}
    const record = raw as { error?: unknown; message?: unknown }
    return {
      ...(typeof record.error === "string" ? { kind: record.error } : {}),
      ...(typeof record.message === "string" ? { message: record.message } : {}),
    }
  } catch {
    return {}
  }
}

export function buildRecorderFailureMessage(detail: CommandFailureDetail): string {
  if (detail.killed) return "camera recorder timed out (duration + safety margin exceeded)"
  const structured = parseRecorderStderr(detail.stderr)
  const kindText = structured.kind ? ` (${structured.kind})` : ""
  const base = structured.message ?? detail.message
  return `camera recorder exited with code ${detail.code ?? "unknown"}${kindText}: ${base}`
}

export async function captureCameraClip(input: CameraCaptureInput): Promise<CameraCaptureResult> {
  const run = input.runCommand ?? defaultRunCommand
  const sourcePath = join(input.extensionDir, "native-camera-recorder.swift")
  const cacheDir = join(input.extensionDir, ".cache")
  const binaryPath = join(cacheDir, "native-camera-recorder")

  if (await needsCompile(sourcePath, binaryPath)) {
    await mkdir(cacheDir, { recursive: true })
    try {
      await run("swiftc", ["-O", sourcePath, "-o", binaryPath], {
        cwd: input.extensionDir,
        timeoutMs: COMPILE_TIMEOUT_MS,
        ...(input.signal ? { signal: input.signal } : {}),
      })
    } catch (error) {
      return { kind: "compile_failed", message: buildCompileFailureMessage(toCommandFailure(error)) }
    }
  }

  await mkdir(dirname(input.outputAbsolutePath), { recursive: true })
  let result: CommandResult
  try {
    result = await run(
      binaryPath,
      [
        "--camera-name",
        input.cameraName,
        "--duration-seconds",
        String(input.durationSeconds),
        "--width",
        String(input.width),
        "--height",
        String(input.height),
        "--capture-fps",
        String(input.captureFps),
        "--output",
        input.outputAbsolutePath,
      ],
      {
        cwd: input.extensionDir,
        timeoutMs: Math.round(input.durationSeconds * 1000) + RECORDER_TIMEOUT_MARGIN_MS,
        ...(input.signal ? { signal: input.signal } : {}),
      },
    )
  } catch (error) {
    const detail = toCommandFailure(error)
    return { kind: "recorder_failed", message: buildRecorderFailureMessage(detail), stderr: detail.stderr }
  }

  const parsed = parseRecorderOutput(result.stdout)
  if (parsed.kind !== "ready") return parsed
  return {
    kind: "ready",
    recording: parsed.recording,
    outputAbsolutePath: input.outputAbsolutePath,
    workspaceRelativePath: relative(input.workspace, input.outputAbsolutePath),
    stderr: result.stderr,
  }
}

function buildCompileFailureMessage(detail: CommandFailureDetail): string {
  const stderr = detail.stderr.trim()
  const suffix = stderr ? `: ${stderr.split("\n").slice(-4).join(" ").slice(0, 600)}` : ""
  return `swiftc failed to compile the recorder (code ${detail.code ?? "unknown"})${suffix}`
}

export function cameraFpsWarning(recording: RecorderMetadata, captureFps: number): string | undefined {
  if (recording.measured_fps === undefined || captureFps <= 0) return undefined
  const ratio = recording.measured_fps / captureFps
  if (ratio >= MEASURED_FPS_WARNING_RATIO) return undefined
  return `warning: measured_fps ${recording.measured_fps} is ${Math.round(ratio * 100)}% of requested capture_fps ${captureFps} (below ${Math.round(MEASURED_FPS_WARNING_RATIO * 100)}% threshold); motion may look choppy but analysis proceeded`
}

function formatRecordingMetadata(recording: RecorderMetadata): string {
  const measured = recording.measured_fps === undefined ? "unavailable" : String(recording.measured_fps)
  return `recording: camera="${recording.camera_name}" ${recording.width}x${recording.height} ${recording.format_subtype} active_fps=${recording.active_fps} measured_fps=${measured} elapsed=${recording.elapsed_seconds}s size=${recording.file_size_bytes}B`
}

export function formatCameraCaptureFailure(
  result: Exclude<CameraCaptureResult, { readonly kind: "ready" }>,
): string {
  return `[camera_video_probe:${result.kind}] ${result.message}`
}

export function formatCameraProbe(input: {
  readonly recording: RecorderMetadata
  readonly captureFps: number
  readonly probe: ProbeResult
}): string {
  const lines = [
    "camera_video_probe recorded a clip and probed it with the active Qianwen model.",
    formatRecordingMetadata(input.recording),
  ]
  const warning = cameraFpsWarning(input.recording, input.captureFps)
  if (warning) lines.push(warning)
  lines.push(formatProbeDiagnostics(input.probe))
  return lines.join("\n")
}
