#include "diagnostic_report.h"

#include "common.h"
#include <algorithm>
#include <cstring>
#include <dxgi1_2.h>
#include <format>
#include <iterator>
#include <sstream>
#include <string_view>
#include <vector>
#include <winternl.h>
#include <wrl/client.h>

namespace cv {
namespace {

constexpr wchar_t kDiagnosticWindowClass[] =
    L"CaptureViewDiagnosticReportWindow";
constexpr int kReportText = 100;
constexpr int kCopyReport = 101;

struct DiagnosticWindowState {
  std::wstring report;
  HWND text = nullptr;
  HWND copy_button = nullptr;
  HWND close_button = nullptr;
  HFONT font = nullptr;
};

std::wstring YesNo(bool value) { return value ? L"Yes" : L"No"; }

std::wstring ValueOrUnavailable(std::wstring_view value) {
  return value.empty() ? L"Unavailable" : std::wstring(value);
}

std::wstring ArchitectureName(WORD architecture) {
  switch (architecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      return L"x64";
    case PROCESSOR_ARCHITECTURE_ARM64:
      return L"Arm64";
    case PROCESSOR_ARCHITECTURE_INTEL:
      return L"x86";
    default:
      return std::format(L"Unknown ({})", architecture);
  }
}

std::wstring WindowsVersion() {
  using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto rtl_get_version = reinterpret_cast<RtlGetVersionFunction>(
      ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr);
  RTL_OSVERSIONINFOW version{
      static_cast<ULONG>(sizeof(RTL_OSVERSIONINFOW))};
  if (!rtl_get_version || rtl_get_version(&version) != 0) {
    return L"Unavailable";
  }
  return std::format(L"{}.{}.{}", version.dwMajorVersion,
                     version.dwMinorVersion, version.dwBuildNumber);
}

std::wstring GraphicsAdapter(std::wstring_view active_adapter) {
  if (!active_adapter.empty()) return std::wstring(active_adapter);
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  DXGI_ADAPTER_DESC1 description{};
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->EnumAdapters1(0, &adapter)) &&
      SUCCEEDED(adapter->GetDesc1(&description))) {
    return description.Description;
  }
  return L"Unavailable";
}

std::wstring HResultSummary(HRESULT result) {
  if (SUCCEEDED(result)) return L"None";
  return std::format(L"0x{:08X} ({})", static_cast<std::uint32_t>(result),
                     HResultText(result));
}

std::wstring PackageKind(bool packaged) {
  return packaged ? L"Microsoft Store / MSIX" : L"Portable or development";
}

struct ResolutionGroup {
  unsigned width = 0;
  unsigned height = 0;
  std::vector<std::wstring> frame_rates;
};

struct SubtypeGroup {
  std::wstring subtype;
  std::vector<ResolutionGroup> resolutions;
};

std::vector<SubtypeGroup> GroupFormats(
    const std::vector<VideoFormatInfo>& formats) {
  std::vector<SubtypeGroup> groups;
  for (const auto& format : formats) {
    auto subtype = std::find_if(
        groups.begin(), groups.end(), [&](const SubtypeGroup& group) {
          return group.subtype == format.subtype_name;
        });
    if (subtype == groups.end()) {
      groups.push_back({format.subtype_name, {}});
      subtype = std::prev(groups.end());
    }
    auto resolution = std::find_if(
        subtype->resolutions.begin(), subtype->resolutions.end(),
        [&](const ResolutionGroup& group) {
          return group.width == format.width && group.height == format.height;
        });
    if (resolution == subtype->resolutions.end()) {
      subtype->resolutions.push_back({format.width, format.height, {}});
      resolution = std::prev(subtype->resolutions.end());
    }
    const double fps = format.frame_rate_denominator
                           ? static_cast<double>(format.frame_rate_numerator) /
                                 format.frame_rate_denominator
                           : 0.0;
    const std::wstring rate = std::format(L"{:.2f}", fps);
    if (std::find(resolution->frame_rates.begin(),
                  resolution->frame_rates.end(), rate) ==
        resolution->frame_rates.end()) {
      resolution->frame_rates.push_back(rate);
    }
  }
  return groups;
}

void LayoutDiagnosticWindow(HWND window, DiagnosticWindowState* state) {
  if (!state) return;
  RECT client{};
  GetClientRect(window, &client);
  const UINT dpi = GetDpiForWindow(window);
  const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
  const int margin = scale(16);
  const int button_width = scale(145);
  const int button_height = scale(34);
  const int button_gap = scale(10);
  const int button_y = client.bottom - margin - button_height;
  MoveWindow(state->text, margin, margin, client.right - margin * 2,
             std::max(1, button_y - margin * 2), TRUE);
  MoveWindow(state->close_button, client.right - margin - button_width,
             button_y, button_width, button_height, TRUE);
  MoveWindow(state->copy_button,
             client.right - margin - button_width * 2 - button_gap, button_y,
             button_width, button_height, TRUE);
}

LRESULT CALLBACK DiagnosticWindowProc(HWND window, UINT message,
                                      WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<DiagnosticWindowState*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    state = static_cast<DiagnosticWindowState*>(
        reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(state));
  }
  switch (message) {
    case WM_CREATE: {
      NONCLIENTMETRICSW metrics{sizeof(metrics)};
      if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                                     &metrics, 0, GetDpiForWindow(window))) {
        state->font = CreateFontIndirectW(&metrics.lfMessageFont);
      }
      const HFONT font = state->font
                             ? state->font
                             : static_cast<HFONT>(
                                   GetStockObject(DEFAULT_GUI_FONT));
      state->text = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_LEFT |
              ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
          0, 0, 0, 0, window,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReportText)), nullptr,
          nullptr);
      SendMessageW(state->text, EM_SETLIMITTEXT, 1024 * 1024, 0);
      SetWindowTextW(state->text, state->report.c_str());
      SendMessageW(state->text, WM_SETFONT, reinterpret_cast<WPARAM>(font),
                   TRUE);
      state->copy_button = CreateWindowW(
          L"BUTTON", L"Copy to clipboard",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0,
          window,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyReport)), nullptr,
          nullptr);
      state->close_button = CreateWindowW(
          L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0,
          0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
          nullptr, nullptr);
      SendMessageW(state->copy_button, WM_SETFONT,
                   reinterpret_cast<WPARAM>(font), TRUE);
      SendMessageW(state->close_button, WM_SETFONT,
                   reinterpret_cast<WPARAM>(font), TRUE);
      LayoutDiagnosticWindow(window, state);
      SetFocus(state->text);
      return 0;
    }
    case WM_SIZE:
      LayoutDiagnosticWindow(window, state);
      return 0;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      const UINT dpi = GetDpiForWindow(window);
      info->ptMinTrackSize = {MulDiv(480, dpi, 96), MulDiv(360, dpi, 96)};
      return 0;
    }
    case WM_MOUSEWHEEL:
      if (state && state->text) {
        SendMessageW(state->text, message, wparam, lparam);
        return 0;
      }
      break;
    case WM_COMMAND:
      if (LOWORD(wparam) == kCopyReport && state) {
        if (CopyTextToClipboard(window, state->report)) {
          MessageBoxW(window,
                      L"The diagnostic report was copied to the clipboard.",
                      L"Copy complete", MB_OK | MB_ICONINFORMATION);
        } else {
          MessageBoxW(window,
                      L"The clipboard is unavailable. The report was not "
                      L"copied.",
                      L"Copy failed", MB_OK | MB_ICONERROR);
        }
        return 0;
      }
      if (LOWORD(wparam) == IDCANCEL) {
        DestroyWindow(window);
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_NCDESTROY:
      if (state) {
        if (state->font) DeleteObject(state->font);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      }
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

bool RegisterDiagnosticWindowClass(HINSTANCE instance) {
  WNDCLASSEXW existing{sizeof(existing)};
  if (GetClassInfoExW(instance, kDiagnosticWindowClass, &existing)) return true;
  WNDCLASSEXW window_class{sizeof(window_class)};
  window_class.lpfnWndProc = DiagnosticWindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground =
      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kDiagnosticWindowClass;
  return RegisterClassExW(&window_class) != 0;
}

}  // namespace

std::wstring BuildDiagnosticReport(const DiagnosticReportData& data) {
  SYSTEM_INFO system{};
  GetNativeSystemInfo(&system);

  std::wostringstream report;
  report << L"CaptureView diagnostic report\r\n"
         << L"\r\n[Application]\r\n"
         << L"Version: " << ValueOrUnavailable(data.app_version) << L"\r\n"
         << L"Distribution: " << PackageKind(data.packaged) << L"\r\n"
         << L"State: " << (data.viewer_active ? L"Viewing" : L"Setup")
         << L"\r\n"
         << L"OS version: " << WindowsVersion() << L"\r\n"
         << L"OS architecture: "
         << ArchitectureName(system.wProcessorArchitecture) << L"\r\n"
         << L"Process architecture: x64\r\n"
         << L"\r\n[Graphics]\r\n"
         << L"Adapter: " << GraphicsAdapter(data.graphics_adapter) << L"\r\n"
         << L"Last renderer path: " << ValueOrUnavailable(data.renderer_path)
         << L"\r\n"
         << L"Last renderer error: " << HResultSummary(data.renderer_error)
         << L"\r\n"
         << L"\r\n[Video]\r\n"
         << L"Device: " << ValueOrUnavailable(data.video_device) << L"\r\n"
         << L"Selected format: "
         << ValueOrUnavailable(data.selected_video_format) << L"\r\n"
         << L"Last native media type: "
         << ValueOrUnavailable(data.native_media_type) << L"\r\n"
         << L"Last negotiated output: "
         << ValueOrUnavailable(data.negotiated_media_type) << L"\r\n"
         << std::format(L"Measured input/render: {:.1f} / {:.1f} fps\r\n",
                        data.input_fps, data.render_fps)
         << L"Received frames: " << data.received_frames << L"\r\n"
         << L"Dropped frames: " << data.dropped_frames << L"\r\n"
         << L"Video queue depth: " << data.video_queue_depth << L"\r\n"
         << L"Last video error: " << HResultSummary(data.video_error)
         << L"\r\n"
         << L"Flip horizontal: " << YesNo(data.flip_horizontal) << L"\r\n"
         << L"Flip vertical: " << YesNo(data.flip_vertical) << L"\r\n"
         << L"Device-advertised formats ("
         << data.supported_video_formats.size() << L" modes):\r\n";
  if (data.supported_video_formats.empty()) {
    report << L"  Unavailable\r\n";
  } else {
    for (const auto& subtype : GroupFormats(data.supported_video_formats)) {
      report << L"  " << subtype.subtype << L":\r\n";
      for (const auto& resolution : subtype.resolutions) {
        report << L"    " << resolution.width << L"x" << resolution.height
               << L": ";
        for (size_t index = 0; index < resolution.frame_rates.size(); ++index) {
          if (index) report << L", ";
          report << resolution.frame_rates[index];
        }
        report << L" fps\r\n";
      }
    }
  }

  report << L"\r\n[Audio]\r\n"
         << L"Input: " << ValueOrUnavailable(data.audio_input) << L"\r\n"
         << L"Output: " << ValueOrUnavailable(data.audio_output) << L"\r\n"
         << L"Running: " << YesNo(data.audio_running) << L"\r\n";
  if (data.audio_sample_rate && data.audio_channels) {
    report << L"Format: " << data.audio_sample_rate << L" Hz, "
           << data.audio_channels << L" channels\r\n";
  } else {
    report << L"Format: Unavailable\r\n";
  }
  report << L"Queue depth: " << data.audio_queue_frames << L" frames\r\n"
         << L"Muted: " << YesNo(data.muted) << L"\r\n"
         << L"Last audio error: " << HResultSummary(data.audio_error)
         << L"\r\n"
         << L"\r\nNo device IDs, serial numbers, account names, or file paths are "
            L"intentionally collected. Review device friendly names before "
            L"sharing.\r\n";
  return report.str();
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
  if (!OpenClipboard(owner)) return false;
  if (!EmptyClipboard()) {
    CloseClipboard();
    return false;
  }
  const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!memory) {
    CloseClipboard();
    return false;
  }
  void* destination = GlobalLock(memory);
  if (!destination) {
    GlobalFree(memory);
    CloseClipboard();
    return false;
  }
  std::memcpy(destination, text.c_str(), bytes);
  GlobalUnlock(memory);
  if (!SetClipboardData(CF_UNICODETEXT, memory)) {
    GlobalFree(memory);
    CloseClipboard();
    return false;
  }
  CloseClipboard();
  return true;
}

void ShowDiagnosticReportDialog(HWND owner, const std::wstring& report) {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!RegisterDiagnosticWindowClass(instance)) {
    MessageBoxW(owner, L"The diagnostic report window could not be created.",
                L"Diagnostic Report", MB_OK | MB_ICONERROR);
    return;
  }

  const UINT dpi = GetDpiForWindow(owner);
  RECT window_rect{0, 0, MulDiv(760, dpi, 96), MulDiv(600, dpi, 96)};
  AdjustWindowRectExForDpi(&window_rect,
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                               WS_THICKFRAME,
                           FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                           dpi);
  RECT owner_rect{};
  GetWindowRect(owner, &owner_rect);
  const LONG width = window_rect.right - window_rect.left;
  const LONG height = window_rect.bottom - window_rect.top;
  LONG left =
      owner_rect.left + (owner_rect.right - owner_rect.left - width) / 2;
  LONG top =
      owner_rect.top + (owner_rect.bottom - owner_rect.top - height) / 2;
  MONITORINFO monitor{sizeof(monitor)};
  if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST),
                      &monitor)) {
    left = std::clamp(left, monitor.rcWork.left,
                      std::max(monitor.rcWork.left, monitor.rcWork.right - width));
    top = std::clamp(top, monitor.rcWork.top,
                     std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
  }

  DiagnosticWindowState state{report};
  HWND dialog = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kDiagnosticWindowClass,
      L"CaptureView Diagnostic Report",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, left, top,
      width, height, owner, nullptr, instance, &state);
  if (!dialog) {
    MessageBoxW(owner, L"The diagnostic report window could not be created.",
                L"Diagnostic Report", MB_OK | MB_ICONERROR);
    return;
  }
  const HICON large_icon = reinterpret_cast<HICON>(
      SendMessageW(owner, WM_GETICON, ICON_BIG, 0));
  const HICON small_icon = reinterpret_cast<HICON>(
      SendMessageW(owner, WM_GETICON, ICON_SMALL, 0));
  if (large_icon) SendMessageW(dialog, WM_SETICON, ICON_BIG,
                               reinterpret_cast<LPARAM>(large_icon));
  if (small_icon) SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                               reinterpret_cast<LPARAM>(small_icon));

  EnableWindow(owner, FALSE);
  ShowWindow(dialog, SW_SHOW);
  UpdateWindow(dialog);
  SetFocus(state.text);
  MSG message{};
  BOOL result = TRUE;
  while (IsWindow(dialog) &&
         (result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    if (!IsDialogMessageW(dialog, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  if (IsWindow(dialog)) DestroyWindow(dialog);
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
  if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
}

}  // namespace cv
