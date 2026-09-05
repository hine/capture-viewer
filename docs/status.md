# Implementation status

This document contains detailed implementation progress and hardware-validation
notes that are intentionally kept out of the public-facing README.

## Release targets

### v1.1.0 — compatibility and lower video overhead (release candidate)

- Feature development is frozen. Sustained-run, interaction, disconnect /
  reconnect, format-transition, and stream-timeout smoke tests pass on
  hardware. Final portable and Store packages have been regenerated and pass
  packaged-build validation. The release candidate is ready for publication.

- The ordered implementation and validation plan is documented in
  [v1.1-roadmap.md](v1.1-roadmap.md).
- The pre-change video pipeline has been audited: every selected native format
  is currently converted or decoded by Media Foundation to RGB32, copied into
  the latest-frame slot, and uploaded to a reusable D3D11 BGRA texture.
- The first hardware gate is validation of the existing YUY2-to-RGB32 path.
  Native NV12 rendering follows only after that gate.
- RGB32 remains the compatibility fallback throughout the 1.1 work.
- The first 640x480/30 YUY2 run displayed the full test chart with plausible
  layout and color. Fine text and line patterns were heavily reduced by the
  1920x1080-to-640x480 source scaling; broader interaction testing remains.
- A PC source matched to 800x600/20 YUY2 displayed correctly with plausible
  color, undistorted 4:3 geometry, and expected pillarboxing. This device is
  accepted for compatibility testing, but its low-rate YUY2 modes are not a
  valid 1080p60 performance benchmark.
- A same-source 1920x1080 comparison showed that 10 fps MJPEG retains materially
  sharper UI text than 5 fps YUY2 on the first capture device. A second device,
  identified as `USB3 Video`, also displayed severe desktop-text degradation at
  1920x1080/20 YUY2 while maintaining the requested frame rate. OBS produced a
  materially sharper result from the same device with YUY2 explicitly forced,
  confirming a CaptureView quality defect in the current MF conversion/render
  path. Diagnostic logging now records native and negotiated subtype, size,
  frame rate, pixel aspect ratio, interlace mode, stride, and sample size.
- The diagnostic run reported progressive (`MFVideoInterlace_Progressive`)
  1920x1080 square-pixel media at 20 fps. Native YUY2 stride/sample size were
  3840/4147200 and negotiated RGB32 values were 7680/8294400, all exact expected
  values. Incorrect geometry, interlacing, pitch, and sample size are therefore
  ruled out; MF's YUY2-to-RGB32 conversion is the leading suspect.
- An initial native-NV12 implementation is pending Windows build and hardware
  validation. It requests NV12 output only after a D3D11 Video Processor and
  NV12 resources have been prepared; otherwise capture retains the existing
  RGB32-compatible path. The first version keeps one CPU copy and one GPU upload
  while moving YUV-to-RGB conversion to the GPU.
- The first 1920x1080/60 NV12 run negotiated native NV12 successfully but showed
  black and terminated inside the first-frame GPU path without an HRESULT being
  logged. Direct `UpdateSubresource` upload to the multi-plane video texture was
  replaced with row-aware mapping of a staging NV12 texture followed by
  `CopyResource`; hardware retesting is pending.
- The staging-upload revision passed its initial `USB3 Video` hardware run at
  native 1920x1080/60 NV12. The image displayed with correct orientation and
  geometry and the overlay measured 60 fps, and the
  process remained running. Resize/fullscreen, color-chart, disconnect, and
  longer-duration checks remain before the native path is accepted.
- The full native-NV12 regression pass subsequently completed without a known
  failure: resize, preset window sizes, fullscreen return, borderless mode,
  overlay toggling, Settings return/restart, sustained playback, and USB
  disconnect/reconnect all passed at 1920x1080/60. Native NV12 is accepted for
  this hardware; broader GPU coverage remains a release-level requirement.
- The validated Video Processor/staging path has been generalized for native
  YUY2. Native YUY2 negotiation and GPU conversion are pending Windows build
  and the same-device comparison against the confirmed sharp OBS result.
- The first native-YUY2 hardware run passed at 1920x1080/60 on `USB3 Video`.
  Previously degraded desktop text rendered sharply, matching the expected OBS
  quality while the overlay measured 60 fps. This
  confirms that bypassing MF's YUY2-to-RGB32 conversion resolves the observed
  fine-detail defect. Full regression testing remains.
- MJPEG capture now prefers Media Foundation decode output in NV12 when the
  renderer has successfully prepared the native NV12 path. Failure to negotiate
  decoded NV12 retains the existing MF decode-to-RGB32 fallback.
- The first 1920x1080/60 MJPEG run after this change displayed a sharp image,
  measured 60 fps, and remained stable. The overlay intentionally
  reports the device-native MJPEG format. The diagnostic log confirmed decoded
  NV12 at 1920x1080/60 and the D3D11 Video Processor NV12 renderer, so the
  optimized decode path is accepted on this hardware.
- The earlier overlay `Queue` value was the audio queue, not a video metric.
  Benchmark instrumentation now reports separate input/render FPS, cumulative
  latest-frame replacements, and the actual 0-or-1 video slot depth; prior
  references to a measured video queue depth have been corrected.
- The new counters passed 1920x1080/60 checks for MJPEG decoded to NV12, native
  NV12, and native YUY2. All three reported 60 fps input, 60 fps render, zero
  cumulative frame replacements, and video slot depth 0 at capture time. The
  audio queue was independently 0 in these snapshots.
- An initial two-process/two-device run passed functionally: native NV12
  1920x1080/60 held 60.0 fps input/render with 9 cumulative replacements, while
  MJPEG-to-NV12 1920x1080/30 held 29.8 fps input/render with 2 replacements.
  Both sampled video slots were empty. The 48 kHz audio queues sampled between
  480 and 960 frames (about 10--20 ms). Task Manager showed the two processes at
  1.9%/27.5 MB and 0.2%/31.2 MB CPU/working set in one snapshot. Total GPU 3D
  usage was about 6% on the Radeon RX 6600 XT, though this includes the desktop
  and other applications and is not attributable to CaptureView alone. A
  longer-duration observation remains.
- A subsequent single-process 1920x1080/60 run remained stable for several
  hours without a visible failure. Task Manager showed CaptureView at about
  1.0% CPU and 28.4 MB working set at the sampled instant. Total Radeon RX 6600
  XT usage was about 4% at 46 degrees C; this remains a system-wide reading.
  The v1.1 sustained-run gate passes on this hardware.
- A VSync diagnostic at native NV12 1920x1080/60 initially held 60.0 fps
  input/render with zero replacements, video slot depth 0, a 10 ms audio queue,
  and about 4% system-wide GPU 3D usage, but became unresponsive after a longer
  idle period. Its log reached normal audio/video shutdown without a render
  error. Blocking `Present(1, 0)` on the UI thread is rejected; production stays
  on non-blocking `Present(0, DXGI_PRESENT_DO_NOT_WAIT)`.
- Audio startup logging now records both endpoint mix formats and the selected
  shared interchange format. Periodic audio statistics were reduced from every
  2 seconds to every 60 seconds to avoid rapid growth during long sessions.
- Matching 48 kHz stereo 32-bit-float input/output mix formats were verified on
  hardware. The input mix format was selected unchanged and monitoring started
  successfully.
- A deliberately mismatched 48 kHz input / 44.1 kHz output test also passed.
  The 48 kHz stereo-float interchange stream was converted by Windows Audio
  Engine and played normally without dropouts, speed changes, or pitch changes.
- A newly acquired capture device advertises native RGB24, including
  1920x1080/60 and 1280x720/120 modes. RGB24 subtype naming was added; it keeps
  using the existing Media Foundation RGB32 conversion/fallback path pending
  hardware validation.
- The first RGB24 1920x1080/60 run exposed a bottom-up frame: its native media
  type reported stride `-5760`, and the converted RGB32 image was vertically
  inverted. Compatible RGB32 buffers now normalize rows through `IMF2DBuffer`
  signed pitch when available.
- OBS XRGB comparison was materially sharper than Media Foundation's converted
  RGB32 output at the same 1920x1080/60 source. RGB24 is therefore retained as
  the Source Reader output and expanded directly from BGR24 to BGRA32 without
  color conversion or interpolation; hardware revalidation remains.
- Direct RGB24 expansion restored fine text detail, matching the OBS-quality
  result. The device's orientation remained inconsistent with the apparent
  stride/pitch metadata, so CaptureView does not encode a device-specific row
  heuristic. Persisted horizontal and vertical correction controls are applied
  by the renderer without adding another frame buffer or transfer. RGB uses a
  shader UV transform; YUV applies correction while filling its existing GPU
  staging texture. Both controls and the corrected 1920x1080/60 RGB24 result
  passed hardware validation. Native NV12 and YUY2 also passed normal display
  and orientation-correction regression checks after the NV12 UV-plane fix.
- The final 1.1 hardware interaction pass covered resize, normal/borderless/
  fullscreen transitions, Settings return and capture restart, clean shutdown,
  USB disconnect recovery, and reconnection. No failure was observed.
- Additional RGB24 transition testing on `USB3 PLUS Video` exposed a
  device-side streaming-state failure. RGB24 1920x1080/60 starts normally after
  USB reconnection and after a YUY2 session, but can stall after MJPEG at either
  30 or 60 fps; an NV12-to-RGB24 transition is intermittent. In the MJPEG case,
  the Source Reader returned an initial stream tick and one uniform-value frame,
  then stopped issuing callbacks despite accepting the next read request.
  The failure occurs before another frame reaches application image processing;
  rendering, RGB expansion, and flip processing are therefore ruled out for
  this reproduced case. USB reconnection restoring direct RGB24 capture further
  identifies it as hardware/driver streaming-state behavior rather than a
  CaptureView format-conversion defect.
- Capture now treats two seconds without a Source Reader callback as a general
  stream timeout and returns through the existing capture-error/setup path.
  Normal NV12, YUY2, and MJPEG playback and YUY2-to-RGB24 transition passed the
  hardware regression check without false timeouts. The timeout message advises
  selecting another format or reconnecting the USB device; reconnection restored
  direct RGB24 capture in testing. No device- or format-pair-specific automatic
  transition workaround is encoded.
- The x64 Release portable package was generated as
  `CaptureView-1.1.0-x64.zip` from the final timeout-aware Release build. Its
  executable exactly matched the verified build, the ZIP contents and SHA-256
  sidecar matched, and the packaged build passed launch, capture, Settings
  return, and shutdown smoke checks. The final ZIP SHA-256 is
  `0d62b9d32913452aad7c05a7d8a345dd607e015d7e0b55f99ce52ff898481001`.
  The final timeout-aware `1.1.0.0` MSIX and symbol-bearing `.msixupload` were
  regenerated from the same Release build. Manifest version/architecture,
  payload, Release-executable identity, and PDB-bearing `.appxsym` structure
  passed inspection. A self-signed copy installed successfully and passed
  version, NV12, YUY2, MJPEG, YUY2-to-RGB24, MJPEG-to-RGB24 timeout, Settings
  return, USB disconnect/reconnect, and shutdown checks. The unsigned MSIX
  SHA-256 is
  `1cd0f8eee30794ef54b3405599bcc9b4809c8ed7cfb7aeea8b177e078ce7184e`;
  the Store submission `.msixupload` SHA-256 is
  `9cb495151d3a87cba1f8b40d55a2dcc4d668ff7e5063dec9b19646b0a169eb1a`.

### v0.9.0 — first public preview (released)

- Released on September 1, 2026 from the public
  [`hine/capture-viewer`](https://github.com/hine/capture-viewer) repository.
- The annotated `v0.9.0` tag identifies the reviewed public source used for the
  release build. The private development repository is not tagged as the public
  release.
- GitHub Actions reproduced the x64 Release build and published a portable ZIP
  with a separate SHA-256 checksum as
  [CaptureView 0.9.0 Preview](https://github.com/hine/capture-viewer/releases/tag/v0.9.0).
- The downloaded release package passed checksum, archive, Windows hardware,
  and post-publication smoke tests. NV12, MJPEG, WASAPI monitoring, Settings
  return, About, and normal shutdown were included in the verified preview set.

### v1.0.0 — first stable and Store release

- Application, About, and Windows executable metadata are verified as `1.0.0`.
- The `1.0.0.0` Store package passed Partner Center validation and certification
  and was published on September 3, 2026.
- The annotated `v1.0.0` tag and stable GitHub Release publish the matching
  source, portable x64 ZIP, and SHA-256 checksum.
- No known release-blocking failure in the advertised NV12/MJPEG video and
  WASAPI audio-monitoring paths.
- Release build, portable package, license inclusion, and MSIX packaging are
  reproducible and tested.
- Setup, normal viewing, Settings return, shutdown, and device-disconnect
  recovery pass a final hardware smoke test.
- Public README, license, About links, release notes, and Store listing are
  complete.
- Profiles/CLI, direct NV12 rendering, numerical latency measurement, and YUY2
  may remain post-1.0 work unless they are advertised as 1.0 features.

Application and Git release versions use three-part Semantic Versioning, such
as `0.9.0` and tag `v0.9.0`. Microsoft Store MSIX identity uses the equivalent
four-part form with a Store-reserved zero revision, such as `1.0.0.0`. Because
an MSIX identity cannot have a zero major version, the first Store package is
planned for the stable `1.0.0` release rather than the `0.9.x` preview series.

| Requirement | Status | Notes |
|---|---|---|
| Multiple process launches | Done | No mutex, IPC, or single-instance behavior |
| Video device enumeration and stable ID | Done | Media Foundation symbolic link |
| Independent audio input/output enumeration | Done | WASAPI endpoint IDs through MMDevice API |
| First-run selection screen | Done | Video/audio device and native video-format selectors verified on Windows |
| Modern setup appearance | Initial verified | DPI-aware card layout, hierarchy, owner-drawn Refresh/Start actions, long Japanese device names, and A-icon-aligned blue palette verified at 150% on Windows |
| Windows visual styles | Initial verified | Common Controls v6 flat combo boxes and Per-Monitor DPI V2 manifest verified on Windows at 150% |
| Application icon | Initial verified | A-concept blue CV/USB mark embedded as a seven-size 16–256px ICO; title-bar and Explorer EXE rendering verified on Windows |
| About/version/license | Initial verified | Context-menu Task Dialog, `1.0.0` version, Windows EXE metadata, author/brand text, repository link, MIT license link, and reserved brand-asset notice verified on Windows |
| Distribution | Stable release published | `0.9.0` is published as a GitHub pre-release. `1.0.0` is published as a stable GitHub Release with an Actions-built portable x64 ZIP, SHA-256 checksum, and full MIT text as `LICENSE.txt`; the matching `1.0.0.0` package is published in the Microsoft Store. Local and GitHub-hosted builds produced valid x64 MSIX and `.msixupload` archives with `.appxsym`/PDB symbols using repository secrets. Self-signing, trust setup, installation, camera/microphone consent, capture, Settings return, shutdown, uninstall, and certificate cleanup passed on Windows. WACK 10.0.26100.7705 returned overall `PASS`; its informational Desktop Bridge test noted the intentional `ShellExecuteW` browser-link call. Partner Center validation and certification passed on September 3, 2026. Packaged and portable builds share `%LOCALAPPDATA%\CaptureView\`; uninstall preserves this data |
| Manual device refresh | Done | Removal, reconnection, endpoint refresh, format refresh, and available-selection preservation verified on hardware |
| User settings | Done | `%LOCALAPPDATA%\CaptureView\settings.json` |
| Logging | Done | Startup, device counts, negotiated video/audio formats, renderer path, bounded periodic statistics, and HRESULT failures |
| D3D11 rendering and aspect fit | Initial verified | RGB32 texture upload, shader display, aspect fit, responsive resize, and window movement verified on Windows |
| Normal/borderless/fullscreen | Done | Borderless fullscreen; style transitions preserve top-level visibility; no display-mode change |
| Always on top/context menu/hotkeys | Done | Viewer shell behavior |
| Capture-relative window sizing | Initial verified | Context menu offers 50/75/100/125/150%; move preserves scale, resize selects Custom, resolution changes reset to 100%, and Per-Monitor DPI V2 prevents 150% bitmap scaling |
| Status overlay | Verified | Toggleable three-line overlay shows device/format, input/render FPS, latest-frame replacements, video slot depth, active audio format, audio queue depth, and mute state |
| Media Foundation video capture | Initial verified | Async Source Reader callback, RGB32 decoder output, latest-frame exchange, D3D11 display, Settings, and exit verified on hardware |
| NV12/YUY2/MJPEG formats | Verified | Native NV12 and YUY2 plus MJPEG-to-NV12 verified through the D3D11 Video Processor at 1920x1080/60 on supporting hardware; RGB32 fallback retained |
| Event-driven WASAPI monitoring | Verified | Matching 48kHz stereo float and mismatched 48kHz-input/44.1kHz-output paths verified; Audio Engine conversion, live mute, Settings restart, bounded queue, and clean exit passed |
| Disconnect recovery | Initial verified | Physical USB removal is detected without freezing; error acknowledgement returns to a fresh setup UI with refreshed device lists |
| Profiles/CLI | Planned | Kept outside the initial shell |

“Done” means implemented in source. The Milestone 1 shell has been built with
MSVC x64 and smoke-tested on Windows with two video devices, four audio inputs,
and eight audio outputs. Video capture has subsequently been verified using an
Ubuntu Server console source at 1280x720/50 NV12, including correct orientation,
color, frame updates, and responsive window operations. Audio monitoring has
also passed an initial hardware smoke test.
