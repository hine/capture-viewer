# Implementation status

This document contains detailed implementation progress and hardware-validation
notes that are intentionally kept out of the public-facing README.

## Release targets

### v1.1.0 — compatibility and lower video overhead (planned)

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
  geometry, the overlay measured 60 fps with video queue depth 0, and the
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
  quality, while the overlay measured 60 fps with video queue depth 0. This
  confirms that bypassing MF's YUY2-to-RGB32 conversion resolves the observed
  fine-detail defect. Full regression testing remains.

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
| Logging | Initial | Startup, device counts, and HRESULT failures |
| D3D11 rendering and aspect fit | Initial verified | RGB32 texture upload, shader display, aspect fit, responsive resize, and window movement verified on Windows |
| Normal/borderless/fullscreen | Done | Borderless fullscreen; style transitions preserve top-level visibility; no display-mode change |
| Always on top/context menu/hotkeys | Done | Viewer shell behavior |
| Capture-relative window sizing | Initial verified | Context menu offers 50/75/100/125/150%; move preserves scale, resize selects Custom, resolution changes reset to 100%, and Per-Monitor DPI V2 prevents 150% bitmap scaling |
| Status overlay | Initial verified | Toggleable two-line overlay shows device/format plus measured video FPS, active audio format, queue depth, and mute state; verified with NV12 and MJPEG |
| Media Foundation video capture | Initial verified | Async Source Reader callback, RGB32 decoder output, latest-frame exchange, D3D11 display, Settings, and exit verified on hardware |
| NV12/YUY2/MJPEG formats | Partial | NV12 verified at 1280x720/50; MJPEG decode/display and continuous scrolling verified at 1920x1080/30; YUY2 remains untested |
| Event-driven WASAPI monitoring | Initial verified | USB Digital Audio monitoring, live mute, Settings stop/restart, and clean exit verified at 48kHz stereo float; no obvious AV skew in a qualitative test |
| Disconnect recovery | Initial verified | Physical USB removal is detected without freezing; error acknowledgement returns to a fresh setup UI with refreshed device lists |
| Profiles/CLI | Planned | Kept outside the initial shell |

“Done” means implemented in source. The Milestone 1 shell has been built with
MSVC x64 and smoke-tested on Windows with two video devices, four audio inputs,
and eight audio outputs. Video capture has subsequently been verified using an
Ubuntu Server console source at 1280x720/50 NV12, including correct orientation,
color, frame updates, and responsive window operations. Audio monitoring has
also passed an initial hardware smoke test.
