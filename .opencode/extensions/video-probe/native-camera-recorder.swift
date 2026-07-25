// native-camera-recorder.swift
//
// Deterministic native macOS AVFoundation camera recorder for the video-probe
// plugin. Selects a camera by its exact localized name (never a drifting
// numeric index), pins the device's native activeFormat and precise
// device-provided frame duration for the requested resolution/fps, records an
// H.264 MP4 via AVCaptureMovieFileOutput, and reports RUNTIME TRUTH (measured
// delivered fps from the recorded file) as one compact JSON object on stdout.
//
// This is reusable CLI infrastructure: no network, no Qianwen/OSS coupling. The
// plugin compiles this once into a cache/runtime path and invokes the binary.
//
// Build:   swiftc -O native-camera-recorder.swift -o native-camera-recorder
// Usage:   native-camera-recorder --camera-name "UGREEN Camera" \
//              --duration-seconds 3 --width 1920 --height 1080 \
//              --capture-fps 30 --output /abs/path/out.mp4
//
// allow: SIZE_OK — single indivisible AVFoundation capture pipeline compiled to
// one cached binary; splitting across files breaks the compile-and-cache story.

import AVFoundation
import CoreMedia
import CoreVideo
import Foundation

// MARK: - Structured exit

/// Exit codes are stable contract for the plugin layer. stderr carries a
/// structured JSON diagnostic; stdout is reserved for the success payload only.
enum ExitCode: Int32 {
    case ok = 0
    case usage = 2
    case cameraMissing = 3
    case permission = 4
    case formatNotFound = 5
    case configFailure = 6
    case recordingFailure = 7
    case outputFailure = 8
}

/// Emit a structured error to stderr and terminate with a nonzero code.
/// Never writes to stdout so the caller can trust stdout is pure JSON success.
func fail(_ code: ExitCode, _ kind: String, _ message: String) -> Never {
    let payload: [String: Any] = ["error": kind, "message": message]
    if let data = try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]),
       let line = String(data: data, encoding: .utf8) {
        FileHandle.standardError.write(Data((line + "\n").utf8))
    } else {
        FileHandle.standardError.write(Data("{\"error\":\"\(kind)\"}\n".utf8))
    }
    exit(code.rawValue)
}

/// Diagnostics-only logging. Always stderr, never stdout.
func diag(_ message: String) {
    FileHandle.standardError.write(Data(("[native-camera-recorder] " + message + "\n").utf8))
}

// MARK: - Argument parsing

struct Options {
    let cameraName: String
    let durationSeconds: Double
    let width: Int32
    let height: Int32
    let captureFps: Double
    let output: String
}

let helpText = """
native-camera-recorder — deterministic macOS AVFoundation H.264 MP4 recorder

USAGE:
  native-camera-recorder --camera-name <name> --duration-seconds <sec> \\
    --width <px> --height <px> --capture-fps <fps> --output <path>

OPTIONS:
  --camera-name <name>       Exact localized device name (e.g. "UGREEN Camera").
                             Matched exactly; numeric indices are never accepted.
  --duration-seconds <sec>   Recording duration in seconds (> 0).
  --width <px>               Exact frame width in pixels.
  --height <px>              Exact frame height in pixels.
  --capture-fps <fps>        Requested frames per second (> 0). The recorder
                             pins the device-provided precise frame duration
                             whose native rate is nearest this value.
  --output <path>            Destination .mp4 path. Parent dir is created if
                             absent. Any explicit path is accepted here; the
                             plugin layer enforces workspace containment.
  -h, --help                 Print this help and exit 0.

OUTPUT:
  On success, one compact JSON object on stdout describing the recording,
  including measured_fps (the RUNTIME-TRUTH delivered rate read back from the
  file, which may differ from the configured active_fps). Diagnostics go to
  stderr. On failure, a structured JSON error on stderr with a nonzero exit.
"""

func parseArguments(_ argv: [String]) -> Options {
    if argv.contains("-h") || argv.contains("--help") {
        print(helpText)
        exit(ExitCode.ok.rawValue)
    }

    var values: [String: String] = [:]
    var index = 0
    while index < argv.count {
        let token = argv[index]
        guard token.hasPrefix("--") else {
            fail(.usage, "usage", "Unexpected argument '\(token)'. Use --help for usage.")
        }
        let key = String(token.dropFirst(2))
        guard index + 1 < argv.count else {
            fail(.usage, "usage", "Missing value for '\(token)'.")
        }
        values[key] = argv[index + 1]
        index += 2
    }

    func required(_ key: String) -> String {
        guard let value = values[key], !value.isEmpty else {
            fail(.usage, "usage", "Missing required option --\(key). Use --help for usage.")
        }
        return value
    }

    func positiveDouble(_ key: String) -> Double {
        let raw = required(key)
        guard let value = Double(raw), value > 0, value.isFinite else {
            fail(.usage, "usage", "--\(key) must be a positive number (got '\(raw)').")
        }
        return value
    }

    func positiveInt32(_ key: String) -> Int32 {
        let raw = required(key)
        guard let value = Int32(raw), value > 0 else {
            fail(.usage, "usage", "--\(key) must be a positive integer (got '\(raw)').")
        }
        return value
    }

    return Options(
        cameraName: required("camera-name"),
        durationSeconds: positiveDouble("duration-seconds"),
        width: positiveInt32("width"),
        height: positiveInt32("height"),
        captureFps: positiveDouble("capture-fps"),
        output: required("output")
    )
}

// MARK: - FourCC helpers

/// Convert a FourCharCode (e.g. media subtype) into its 4-character string.
func fourCCString(_ code: FourCharCode) -> String {
    let bytes = [
        UInt8((code >> 24) & 0xFF),
        UInt8((code >> 16) & 0xFF),
        UInt8((code >> 8) & 0xFF),
        UInt8(code & 0xFF),
    ]
    let scalars = bytes.map { byte -> Character in
        (0x20...0x7E).contains(byte) ? Character(UnicodeScalar(byte)) : "?"
    }
    return String(scalars)
}

// The preferred pixel subtype when multiple exact-dimension candidates support
// the requested rate: 4:2:0 bi-planar video-range ('420v'), then full-range.
let preferred420v: FourCharCode = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
let preferred420f: FourCharCode = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange

// MARK: - Camera discovery

func discoverCamera(named name: String) -> AVCaptureDevice {
    let deviceTypes: [AVCaptureDevice.DeviceType] = [
        .builtInWideAngleCamera,
        .external,
        .continuityCamera,
        .deskViewCamera,
    ]
    let session = AVCaptureDevice.DiscoverySession(
        deviceTypes: deviceTypes,
        mediaType: .video,
        position: .unspecified
    )
    let devices = session.devices
    guard let device = devices.first(where: { $0.localizedName == name }) else {
        let available = devices.map { "\"\($0.localizedName)\"" }.joined(separator: ", ")
        fail(.cameraMissing, "camera_missing",
             "No video device with exact localized name \"\(name)\". Available: [\(available)]")
    }
    return device
}

// MARK: - Permission gate

func ensureCameraAuthorized() {
    switch AVCaptureDevice.authorizationStatus(for: .video) {
    case .authorized:
        return
    case .notDetermined:
        let semaphore = DispatchSemaphore(value: 0)
        var granted = false
        AVCaptureDevice.requestAccess(for: .video) { ok in
            granted = ok
            semaphore.signal()
        }
        // Not on the main run loop path; a bounded wait here cannot deadlock
        // the capture delegate, which runs on its own queue.
        semaphore.wait()
        if !granted {
            fail(.permission, "permission_denied",
                 "Camera access was denied when prompted. Grant access in System Settings > Privacy & Security > Camera.")
        }
    case .denied:
        fail(.permission, "permission_denied",
             "Camera access is denied for this process. Grant it in System Settings > Privacy & Security > Camera.")
    case .restricted:
        fail(.permission, "permission_denied",
             "Camera access is restricted on this system (e.g. by MDM/parental controls).")
    @unknown default:
        fail(.permission, "permission_denied", "Camera authorization is in an unknown state.")
    }
}

// MARK: - Format & frame-duration selection

struct FormatChoice {
    let format: AVCaptureDevice.Format
    let frameDuration: CMTime
    let activeFps: Double
    let subtype: FourCharCode
}

/// Dimensions of a capture format from its underlying CMVideoFormatDescription.
func dimensions(of format: AVCaptureDevice.Format) -> CMVideoDimensions {
    CMVideoFormatDescriptionGetDimensions(format.formatDescription)
}

func subtype(of format: AVCaptureDevice.Format) -> FourCharCode {
    CMFormatDescriptionGetMediaSubType(format.formatDescription)
}

/// Rank preferred pixel subtypes: 420v first, then 420f, then everything else.
func subtypeRank(_ code: FourCharCode) -> Int {
    if code == preferred420v { return 0 }
    if code == preferred420f { return 1 }
    return 2
}

/// Whether any of the format's native frame-rate ranges contains `fps`.
func frameRateRangeContains(_ format: AVCaptureDevice.Format, _ fps: Double) -> Bool {
    let eps = 0.01
    return format.videoSupportedFrameRateRanges.contains { range in
        fps >= range.minFrameRate - eps && fps <= range.maxFrameRate + eps
    }
}

/// The device-provided precise frame duration nearest the requested fps.
///
/// For a fixed-rate range (native 30.00003), we return the device's own CMTime
/// (e.g. 1001/30030) rather than a hand-written 1/30 — preserving the exact
/// hardware cadence. For a continuous range with fps strictly inside, we
/// synthesize a CMTime for the requested fps; at the endpoints we still prefer
/// the device-provided duration.
func preciseFrameDuration(for format: AVCaptureDevice.Format, requestedFps fps: Double) -> CMTime? {
    let eps = 0.01
    var best: (duration: CMTime, diff: Double)?

    func consider(_ duration: CMTime, rate: Double) {
        guard duration.isValid, duration.value > 0 else { return }
        let diff = abs(rate - fps)
        if best == nil || diff < best!.diff {
            best = (duration, diff)
        }
    }

    for range in format.videoSupportedFrameRateRanges {
        guard fps >= range.minFrameRate - eps, fps <= range.maxFrameRate + eps else { continue }

        if abs(range.minFrameRate - range.maxFrameRate) < eps {
            // Fixed-rate format: honor the device's exact frame duration.
            consider(range.minFrameDuration, rate: range.maxFrameRate)
            consider(range.maxFrameDuration, rate: range.minFrameRate)
            continue
        }

        // Continuous range.
        if abs(fps - range.maxFrameRate) < eps {
            consider(range.minFrameDuration, rate: range.maxFrameRate)
        } else if abs(fps - range.minFrameRate) < eps {
            consider(range.maxFrameDuration, rate: range.minFrameRate)
        } else {
            // fps strictly inside a continuous range: synthesize exact cadence.
            let synthesized = CMTime(value: 1_000_000, timescale: Int32((fps * 1_000_000).rounded()))
            consider(synthesized, rate: fps)
        }
    }

    return best?.duration
}

func chooseFormat(device: AVCaptureDevice, options: Options) -> FormatChoice {
    let candidates = device.formats.filter { format in
        let dims = dimensions(of: format)
        return dims.width == options.width
            && dims.height == options.height
            && frameRateRangeContains(format, options.captureFps)
    }

    guard !candidates.isEmpty else {
        let seen = Set(device.formats.map { dims -> String in
            let d = dimensions(of: dims)
            return "\(d.width)x\(d.height)"
        }).sorted().joined(separator: ", ")
        fail(.formatNotFound, "format_not_found",
             "No \(options.width)x\(options.height) format on \"\(options.cameraName)\" whose native frame-rate range contains \(options.captureFps) fps. Available dimensions: [\(seen)]")
    }

    // Prefer 420v, then 420f, then any; stable within a rank.
    let sorted = candidates.sorted { lhs, rhs in
        subtypeRank(subtype(of: lhs)) < subtypeRank(subtype(of: rhs))
    }

    for format in sorted {
        guard let duration = preciseFrameDuration(for: format, requestedFps: options.captureFps) else {
            continue
        }
        let activeFps = Double(duration.timescale) / Double(duration.value)
        return FormatChoice(
            format: format,
            frameDuration: duration,
            activeFps: activeFps,
            subtype: subtype(of: format)
        )
    }

    fail(.formatNotFound, "format_not_found",
         "Matching \(options.width)x\(options.height) format exists but no precise frame duration could be derived for \(options.captureFps) fps.")
}

// MARK: - Recording delegate

/// Bridges the AVFoundation delegate callback (delivered on the capture queue)
/// back to the main run loop, which is being pumped by the recorder. State is
/// guarded by a lock; @unchecked Sendable is sound because every access is
/// serialized through `lock`.
final class RecordingDelegate: NSObject, AVCaptureFileOutputRecordingDelegate, @unchecked Sendable {
    private let lock = NSLock()
    private var finishedError: Error?
    private var didFinish = false

    func fileOutput(
        _ output: AVCaptureFileOutput,
        didFinishRecordingTo outputFileURL: URL,
        from connections: [AVCaptureConnection],
        error: Error?
    ) {
        lock.lock()
        finishedError = error
        didFinish = true
        lock.unlock()
        // Wake the pumped main run loop from any thread.
        DispatchQueue.main.async {
            CFRunLoopStop(CFRunLoopGetMain())
        }
    }

    func isFinished() -> Bool {
        lock.lock(); defer { lock.unlock() }
        return didFinish
    }

    func completionError() -> Error? {
        lock.lock(); defer { lock.unlock() }
        return finishedError
    }
}

// MARK: - Runtime-truth metadata

struct MeasuredMedia {
    let frameCount: Int
    let mediaDurationSeconds: Double
    let measuredFps: Double
}

/// Read the recorded file back and count real video frames — the delivered fps
/// (runtime truth) rather than the configured cadence. Uses AVAssetReader
/// synchronously; runs after recording finished, so it cannot deadlock the
/// capture pipeline.
func measureRecordedMedia(url: URL) -> MeasuredMedia? {
    let asset = AVURLAsset(url: url)

    let semaphore = DispatchSemaphore(value: 0)
    var loadedTracks: [AVAssetTrack] = []
    asset.loadTracks(withMediaType: .video) { tracks, _ in
        loadedTracks = tracks ?? []
        semaphore.signal()
    }
    semaphore.wait()

    guard let track = loadedTracks.first else { return nil }
    guard let reader = try? AVAssetReader(asset: asset) else { return nil }

    let output = AVAssetReaderTrackOutput(track: track, outputSettings: nil)
    output.alwaysCopiesSampleData = false
    guard reader.canAdd(output) else { return nil }
    reader.add(output)
    guard reader.startReading() else { return nil }

    var count = 0
    var minPTS = Double.greatestFiniteMagnitude
    var maxPTS = -Double.greatestFiniteMagnitude
    var lastDuration = 0.0

    while let sample = output.copyNextSampleBuffer() {
        let pts = CMSampleBufferGetPresentationTimeStamp(sample)
        if pts.isValid {
            let seconds = CMTimeGetSeconds(pts)
            minPTS = min(minPTS, seconds)
            maxPTS = max(maxPTS, seconds)
        }
        let dur = CMSampleBufferGetOutputDuration(sample)
        if dur.isValid, dur.value > 0 {
            lastDuration = CMTimeGetSeconds(dur)
        }
        count += 1
    }

    guard count > 0 else { return nil }

    let span = (maxPTS - minPTS)
    // Average inter-frame rate is the most honest delivered-fps estimate.
    let measuredFps = count > 1 && span > 0 ? Double(count - 1) / span : 0
    let mediaDuration = span > 0 ? span + max(lastDuration, 0) : 0

    return MeasuredMedia(frameCount: count, mediaDurationSeconds: mediaDuration, measuredFps: measuredFps)
}

// MARK: - Output path preparation

func prepareOutputURL(_ path: String) -> URL {
    let url = URL(fileURLWithPath: path)
    let parent = url.deletingLastPathComponent()
    do {
        try FileManager.default.createDirectory(at: parent, withIntermediateDirectories: true)
    } catch {
        fail(.outputFailure, "output_failure",
             "Could not create output parent directory \"\(parent.path)\": \(error.localizedDescription)")
    }
    // AVCaptureMovieFileOutput refuses to overwrite an existing file.
    if FileManager.default.fileExists(atPath: url.path) {
        do {
            try FileManager.default.removeItem(at: url)
        } catch {
            fail(.outputFailure, "output_failure",
                 "Output path exists and could not be replaced \"\(url.path)\": \(error.localizedDescription)")
        }
    }
    return url
}

// MARK: - Recording pipeline

func run(_ options: Options) {
    let outputURL = prepareOutputURL(options.output)

    ensureCameraAuthorized()
    let device = discoverCamera(named: options.cameraName)
    let choice = chooseFormat(device: device, options: options)

    let session = AVCaptureSession()

    let input: AVCaptureDeviceInput
    do {
        input = try AVCaptureDeviceInput(device: device)
    } catch {
        fail(.configFailure, "config_failure",
             "Could not open capture input for \"\(options.cameraName)\": \(error.localizedDescription)")
    }

    session.beginConfiguration()
    guard session.canAddInput(input) else {
        session.commitConfiguration()
        fail(.configFailure, "config_failure", "Session refused the capture input for \"\(options.cameraName)\".")
    }
    session.addInput(input)
    // On macOS, setting device.activeFormat below automatically switches the
    // session preset to InputPriority, so the chosen format dictates QoS and is
    // never overridden by a preset. (The InputPriority constant itself is
    // iOS-only; the behavior is implicit here.)

    let movieOutput = AVCaptureMovieFileOutput()
    guard session.canAddOutput(movieOutput) else {
        session.commitConfiguration()
        fail(.configFailure, "config_failure", "Session refused the movie file output.")
    }
    session.addOutput(movieOutput)
    session.commitConfiguration()

    // Pin the device's native format and precise frame duration.
    do {
        try device.lockForConfiguration()
        device.activeFormat = choice.format
        device.activeVideoMinFrameDuration = choice.frameDuration
        device.activeVideoMaxFrameDuration = choice.frameDuration
        device.unlockForConfiguration()
    } catch {
        fail(.configFailure, "config_failure",
             "Could not lock device for configuration: \(error.localizedDescription)")
    }

    // Fail closed: validate the locked dimensions actually match the request.
    let lockedDims = dimensions(of: device.activeFormat)
    guard lockedDims.width == options.width, lockedDims.height == options.height else {
        fail(.configFailure, "config_failure",
             "Active format after lock is \(lockedDims.width)x\(lockedDims.height), not requested \(options.width)x\(options.height). Refusing to record wrong resolution.")
    }

    guard let videoConnection = movieOutput.connection(with: .video) else {
        fail(.configFailure, "config_failure", "No video connection on the movie file output.")
    }
    movieOutput.setOutputSettings([AVVideoCodecKey: AVVideoCodecType.h264], for: videoConnection)

    // Stop automatically at the requested duration.
    movieOutput.maxRecordedDuration = CMTime(seconds: options.durationSeconds, preferredTimescale: 600)

    let delegate = RecordingDelegate()

    session.startRunning()
    guard session.isRunning else {
        fail(.recordingFailure, "recording_failure", "Capture session failed to start running.")
    }

    let startTime = Date()
    movieOutput.startRecording(to: outputURL, recordingDelegate: delegate)

    // Run-loop pump: the delegate fires on the capture queue and stops this
    // loop via the main run loop. A safety deadline guards against a stuck
    // pipeline so the process always terminates.
    let deadline = Date().addingTimeInterval(options.durationSeconds + 15.0)
    while !delegate.isFinished() {
        let remaining = deadline.timeIntervalSinceNow
        if remaining <= 0 {
            if movieOutput.isRecording {
                movieOutput.stopRecording()
            }
            _ = RunLoop.main.run(mode: .default, before: Date().addingTimeInterval(2.0))
            break
        }
        _ = RunLoop.main.run(mode: .default, before: Date().addingTimeInterval(min(0.25, remaining)))
    }

    let elapsedSeconds = Date().timeIntervalSince(startTime)
    session.stopRunning()

    // A max-duration stop surfaces as an error we must treat as SUCCESS.
    if let error = delegate.completionError() {
        let nsError = error as NSError
        let isMaxDuration = nsError.domain == AVFoundationErrorDomain
            && nsError.code == AVError.Code.maximumDurationReached.rawValue
        if !isMaxDuration {
            fail(.recordingFailure, "recording_failure",
                 "Recording did not complete cleanly: \(error.localizedDescription)")
        }
    }

    guard FileManager.default.fileExists(atPath: outputURL.path) else {
        fail(.recordingFailure, "recording_failure", "Recording finished but no output file was produced.")
    }

    let attributes = try? FileManager.default.attributesOfItem(atPath: outputURL.path)
    let fileSize = (attributes?[.size] as? NSNumber)?.intValue ?? 0
    if fileSize <= 0 {
        fail(.recordingFailure, "recording_failure", "Output file is empty (0 bytes).")
    }

    let measured = measureRecordedMedia(url: outputURL)

    emitSuccess(options: options, choice: choice, outputURL: outputURL,
                elapsedSeconds: elapsedSeconds, fileSize: fileSize, measured: measured)
}

// MARK: - Success payload

func emitSuccess(
    options: Options,
    choice: FormatChoice,
    outputURL: URL,
    elapsedSeconds: Double,
    fileSize: Int,
    measured: MeasuredMedia?
) {
    let lockedDims = dimensions(of: choice.format)
    var payload: [String: Any] = [
        "output_path": outputURL.path,
        "camera_name": options.cameraName,
        "format_subtype": fourCCString(choice.subtype),
        "width": Int(lockedDims.width),
        "height": Int(lockedDims.height),
        "requested_fps": options.captureFps,
        "active_fps": roundTo(choice.activeFps, places: 5),
        "requested_duration_seconds": options.durationSeconds,
        "elapsed_seconds": roundTo(elapsedSeconds, places: 3),
        "file_size_bytes": fileSize,
        "codec": "h264",
    ]

    if let measured {
        payload["measured_fps"] = roundTo(measured.measuredFps, places: 3)
        payload["measured_frame_count"] = measured.frameCount
        payload["media_duration_seconds"] = roundTo(measured.mediaDurationSeconds, places: 3)
    } else {
        diag("Could not read back media metadata; measured_fps omitted.")
    }

    guard let data = try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]),
          let line = String(data: data, encoding: .utf8) else {
        fail(.outputFailure, "output_failure", "Failed to serialize success payload.")
    }
    print(line)
    exit(ExitCode.ok.rawValue)
}

func roundTo(_ value: Double, places: Int) -> Double {
    guard value.isFinite else { return 0 }
    let factor = pow(10.0, Double(places))
    return (value * factor).rounded() / factor
}

// MARK: - Entry point

let options = parseArguments(Array(CommandLine.arguments.dropFirst()))
run(options)
