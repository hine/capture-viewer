# Architecture and delivery plan

## Principles

- One process owns one video capture pipeline. There is deliberately no global mutex or IPC forwarding.
- Device identity is persisted by Media Foundation symbolic link / WASAPI endpoint ID, not display name.
- Capture, rendering, and UI are separate components. The capture callback will publish only the newest frame; an older unpublished frame is replaced instead of queued.
- D3D11 resources are owned by `Renderer`; the forthcoming Media Foundation source-reader path can negotiate a D3D-backed NV12 surface without changing window code.
- User-facing errors carry a plain-language message while the log retains the HRESULT.

## Milestones

1. **Application shell (implemented):** Win32 lifecycle, MF and COM startup, video/audio endpoint enumeration, selection UI, JSON settings, logging, D3D11 swap chain, aspect-fit viewport and window modes.
2. **Video MVP (implemented baseline):** native format enumeration/selection (NV12, YUY2, MJPEG, RGB32), asynchronous Source Reader callback, latest-frame handoff, RGB32 texture rendering, and disconnect errors.
3. **Audio MVP (implemented):** independent WASAPI input/output selection, event-driven shared-mode path, bounded queue, overflow dropping, live mute, and Audio Engine format conversion are implemented and hardware-validated.
4. **Version 1.1 video efficiency:** YUY2 compatibility validation, native NV12 and YUY2 D3D11 rendering with RGB32 fallback, MJPEG decode-to-NV12 where available, and measured Present behavior.
5. **Later candidates:** `--profile`, full CLI overrides, optional VSync control, and AV delay.

The capture worker replaces its unpublished frame instead of queuing frames and
posts at most one pending frame notification to the UI. Rendering therefore
tracks the newest available sample rather than accumulating latency. The UI
thread renders in response to that notification; the timer performs only the
initial clear/present before the first captured frame.

Before the 1.1 work, the implementation asked Media Foundation for RGB32 output
regardless of whether the selected input was NV12, YUY2, MJPEG, or RGB32. That
audited compatibility path remains for MJPEG/RGB32 and as fallback. For native
NV12 and YUY2, the renderer now preflights D3D11 Video Processor resources,
capture retains the selected YUV subtype, and each frame is copied into a
mapped staging texture before `CopyResource` and GPU YUV-to-RGB conversion. The
latest-frame bound remains one and the texture resources are reused.

`Present(0, DXGI_PRESENT_DO_NOT_WAIT)` avoids a vertical-blank wait on the UI
thread. See [v1.1-roadmap.md](v1.1-roadmap.md) for the audited baseline, ordered
native-YUV work, and hardware test matrix.

The initial end-to-end hardware test used a USB capture device with a
1280x720/50 NV12 native format and an Ubuntu Server console source. Orientation,
color, live updates, aspect handling, and window responsiveness passed.
Media Foundation's MJPEG decoder path was also verified for correct image,
orientation, and color at 1920x1080/30. A subsequent continuous console-scroll
test showed no visible frame-pacing, responsiveness, or stability problems.

The asynchronous Source Reader detects physical USB removal and reports the
Media Foundation invalidated-device error without blocking the UI. Setup,
capture-error, and audio-error transitions share one path that recreates the
top-level setup HWND and re-enumerates all endpoints; this also clears any DWM
flip-model surface retained from the viewer HWND.
The full removal flow—capture, unplug, error acknowledgement, fresh setup UI,
and removal of the missing endpoint from the list—passed on hardware.
The setup screen also provides manual device refresh. Removing and reconnecting
the USB capture, refreshing video/audio endpoints and native formats, and
preserving still-available selections passed without restarting the process.

The capture worker also monitors Source Reader callback progress independently
of pixel format. If no callback arrives for two seconds, it reports a timeout
through the normal capture-error path instead of leaving a black viewer that
appears to be running. Recovery deliberately does not force a hard-coded
intermediate format: advertised format transitions are device-dependent, and a
valid black input cannot be identified reliably from pixel values alone. The
setup error instead recommends choosing another format or physically
reconnecting the device, which commonly resets device-side streaming state.
The reproduced failure is classified as hardware/driver-side behavior: Media
Foundation accepted the target type and the next read request, but Source Reader
callbacks ceased before another sample entered CaptureView's buffer conversion
or rendering path. This classification applies to that measured transition and
does not assume that every black-frame symptom has the same cause.

The initial audio path first tries the selected input endpoint's mix format on
the output, then tries the output mix format on the input. It then negotiates
common 48kHz or 44.1kHz stereo float/PCM16 stream formats, letting each shared
Windows Audio Engine endpoint convert between that stream format and its own
mix format. `AUTOCONVERTPCM` and `SRC_DEFAULT_QUALITY` are enabled when the
endpoints do not expose an exact common format. The bounded queue retains at most roughly two output
buffers and drops its oldest complete-frame-aligned data on overflow. Muting
continues to consume queued data so unmuting cannot replay stale audio. Endpoint
mix formats and the selected interchange format are logged at startup so
hardware conversion behavior can be verified without a custom resampler.

The USB capture hardware exposes its HDMI audio separately as `Digital Input
(USB Digital Audio)`. End-to-end monitoring from that endpoint to an explicitly
selected output was verified at 48kHz, stereo, 32-bit float. The pipeline logs
two-second capture/render/queue statistics for diagnosing silence or dropouts.
Mute/unmute, Settings stop/restart, and application shutdown were also verified.
A qualitative viewing/listening test found no obvious AV skew or objectionable
latency. The approximately 10ms impression is not treated as a measurement;
numeric latency still requires a synchronized flash/click source and high-speed
recording or equivalent instrumentation.

## Runtime data

```text
%LOCALAPPDATA%\CaptureView\
  settings.json
  logs\captureview.log
```
