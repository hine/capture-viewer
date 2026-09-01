# Implementation status

This document contains detailed implementation progress and hardware-validation
notes that are intentionally kept out of the public-facing README.

## Release targets

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
| About/version/license | Initial verified | Context-menu Task Dialog, `0.9.0` version, author/brand text, repository link, MIT license link, and reserved brand-asset notice verified on Windows |
| Distribution | Preview released; MSIX smoke-tested | `0.9.0` is published as a GitHub pre-release with an Actions-built portable x64 ZIP, SHA-256 checksum, and full MIT text as `LICENSE.txt`. Local and GitHub-hosted builds produced valid x64 MSIX and `.msixupload` archives with `.appxsym`/PDB symbols using repository secrets. Self-signing, trust setup, installation, camera/microphone consent, capture, Settings return, shutdown, uninstall, and certificate cleanup passed on Windows. Packaged and portable builds share `%LOCALAPPDATA%\CaptureView\`; uninstall preserves this data. WACK and Partner Center validation remain. `1.0.0`/MSIX `1.0.0.0` is the first stable Store target |
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
