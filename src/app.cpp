#include "app.h"
#include "common.h"
#include "logger.h"
#include "resource.h"
#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <format>
#include <mfapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

namespace cv {
namespace {
constexpr wchar_t kClassName[] = L"CaptureViewWindow";
constexpr UINT_PTR kRenderTimer = 1;
constexpr UINT kStartViewerMessage = WM_APP + 1;
constexpr UINT kVideoFrameMessage = WM_APP + 2;
constexpr UINT kCaptureErrorMessage = WM_APP + 3;
constexpr UINT kAudioErrorMessage = WM_APP + 4;
constexpr UINT kShowSettingsMessage = WM_APP + 5;
constexpr UINT kRefreshDevicesMessage = WM_APP + 6;
constexpr int kVideoCombo = 101, kAudioInCombo = 102, kAudioOutCombo = 103,
              kStart = 104, kVideoFormatCombo = 105, kRefreshDevices = 106;
constexpr int kSetupTitle = 110, kSetupSubtitle = 111, kSetupHelper = 112,
              kSetupNotice = 113;
constexpr int kMenuFullscreen = 201, kMenuBorderless = 202, kMenuTopmost = 203,
              kMenuMute = 204, kMenuSettings = 205, kMenuExit = 206,
              kMenuOverlay = 207, kMenuAbout = 208,
              kMenuFlipHorizontal = 209, kMenuFlipVertical = 210,
              kMenuScale50 = 250, kMenuScale75 = 275,
              kMenuScale100 = 300, kMenuScale125 = 325, kMenuScale150 = 350;
constexpr COLORREF kSetupBackground = RGB(246, 248, 251);
constexpr COLORREF kSetupCard = RGB(255, 255, 255);
constexpr COLORREF kSetupBorder = RGB(218, 224, 232);
constexpr COLORREF kSetupText = RGB(29, 42, 58);
constexpr COLORREF kSetupMuted = RGB(101, 113, 128);
// Shared with the planned blue A icon.
constexpr COLORREF kAccent = RGB(24, 82, 148);
constexpr COLORREF kAccentPressed = RGB(17, 63, 116);
constexpr wchar_t kRepositoryUrl[] = L"https://github.com/hine/capture-viewer";
constexpr wchar_t kLicenseUrl[] =
    L"https://github.com/hine/capture-viewer/blob/main/LICENSE";

#define CV_WIDEN_INNER(value) L##value
#define CV_WIDEN(value) CV_WIDEN_INNER(value)
constexpr wchar_t kVersion[] = CV_WIDEN(CAPTUREVIEW_VERSION);

HRESULT CALLBACK AboutDialogCallback(HWND, UINT notification, WPARAM,
                                     LPARAM data, LONG_PTR) {
  if (notification == TDN_HYPERLINK_CLICKED && data) {
    ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(data), nullptr,
                  nullptr, SW_SHOWNORMAL);
  }
  return S_OK;
}

std::wstring SelectedId(HWND combo, const std::vector<DeviceInfo>& devices) {
  const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  return index >= 0 && static_cast<size_t>(index) < devices.size() ? devices[index].id : L"";
}
void SelectSaved(HWND combo, const std::vector<DeviceInfo>& devices, const std::wstring& id) {
  auto found = std::find_if(devices.begin(), devices.end(), [&](const DeviceInfo& d) { return d.id == id; });
  SendMessageW(combo, CB_SETCURSEL, found == devices.end() ? (devices.empty() ? -1 : 0)
                                                        : std::distance(devices.begin(), found), 0);
}
}

int App::Run(HINSTANCE instance, int show_command) {
  settings_path_ = AppDataRoot() / L"settings.json";
  settings_ = LoadSettings(settings_path_);
  videos_ = EnumerateVideoDevices();
  audio_inputs_ = EnumerateAudioInputs();
  audio_outputs_ = EnumerateAudioOutputs();
  Logger::Instance().Info(std::format(L"Devices: video={}, audio inputs={}, audio outputs={}",
                                      videos_.size(), audio_inputs_.size(), audio_outputs_.size()));
  if (!CreateMainWindow(instance, show_command)) return 1;
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message); DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

bool App::CreateMainWindow(HINSTANCE instance, int show_command) {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_CAPTUREVIEW));
  wc.hIconSm = static_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(IDI_CAPTUREVIEW), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  if (!RegisterClassExW(&wc)) return false;
  const UINT dpi = GetDpiForSystem();
  const int width = MulDiv(640, dpi, 96);
  const int height = MulDiv(400, dpi, 96);
  window_ = CreateWindowExW(0, kClassName, L"CaptureView", WS_OVERLAPPEDWINDOW,
      settings_.window.left, settings_.window.top, width, height, nullptr, nullptr, instance, this);
  if (!window_) return false;
  const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                        sizeof(corner));
  ShowWindow(window_, show_command); UpdateWindow(window_); return true;
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    app->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
  }
  return app ? app->HandleMessage(window, message, wparam, lparam)
             : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT App::HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE: CreateSetupControls(); return 0;
    case WM_COMMAND:
      if (LOWORD(wparam) == kVideoCombo && HIWORD(wparam) == CBN_SELCHANGE) {
        RefreshVideoFormats();
        return 0;
      }
      switch (LOWORD(wparam)) {
        // Do not destroy the notifying button and combo boxes while handling
        // their WM_COMMAND call stack. Defer the screen transition until the
        // message loop regains control.
        case kStart: PostMessageW(window_, kStartViewerMessage, 0, 0); break;
        case kRefreshDevices:
          PostMessageW(window_, kRefreshDevicesMessage, 0, 0);
          break;
        case kMenuFullscreen: ToggleFullscreen(); break;
        case kMenuBorderless: SetBorderless(!settings_.borderless); break;
        case kMenuTopmost:
          settings_.always_on_top = !settings_.always_on_top;
          SetWindowPos(window_, settings_.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                       0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); break;
        case kMenuMute:
          settings_.muted = !settings_.muted;
          audio_.SetMuted(settings_.muted);
          break;
        case kMenuOverlay:
          settings_.status_overlay = !settings_.status_overlay;
          UpdateStatusOverlay();
          SaveSettings(settings_path_, settings_);
          break;
        case kMenuFlipHorizontal:
          settings_.flip_horizontal = !settings_.flip_horizontal;
          renderer_.SetFlip(settings_.flip_horizontal, settings_.flip_vertical);
          SaveSettings(settings_path_, settings_);
          break;
        case kMenuFlipVertical:
          settings_.flip_vertical = !settings_.flip_vertical;
          renderer_.SetFlip(settings_.flip_horizontal, settings_.flip_vertical);
          SaveSettings(settings_path_, settings_);
          break;
        case kMenuAbout: ShowAbout(); break;
        case kMenuScale50: SetWindowScale(50); break;
        case kMenuScale75: SetWindowScale(75); break;
        case kMenuScale100: SetWindowScale(100); break;
        case kMenuScale125: SetWindowScale(125); break;
        case kMenuScale150: SetWindowScale(150); break;
        case kMenuSettings:
          PostMessageW(window_, kShowSettingsMessage, 0, 0);
          break;
        case kMenuExit:
          PostMessageW(window_, WM_CLOSE, 0, 0);
          break;
      }
      return 0;
    case WM_DRAWITEM:
      if (!viewer_mode_ &&
          (wparam == kStart || wparam == kRefreshDevices)) {
        DrawSetupButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
        return TRUE;
      }
      break;
    case WM_CTLCOLORSTATIC:
      if (!viewer_mode_) {
        HDC dc = reinterpret_cast<HDC>(wparam);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, id == kSetupSubtitle || id == kSetupHelper ||
                              id == kSetupNotice ? kSetupMuted : kSetupText);
        return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
      }
      break;
    case kStartViewerMessage:
      if (!viewer_mode_) StartViewer();
      return 0;
    case kRefreshDevicesMessage:
      if (!viewer_mode_) RefreshDevices();
      return 0;
    case kShowSettingsMessage:
      if (viewer_mode_) {
        audio_.Stop();
        capture_.Stop();
        RecreateSetupWindow();
      }
      return 0;
    case kVideoFrameMessage: {
      VideoFrame frame;
      if (viewer_mode_ && capture_.TakeLatestFrame(frame)) {
        ++overlay_frame_count_;
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - overlay_sample_start_).count();
        if (seconds >= 1.0) {
          const std::uint64_t received = capture_.ReceivedFrames();
          measured_input_fps_ =
              (received - overlay_last_received_frames_) / seconds;
          overlay_last_received_frames_ = received;
          measured_fps_ = overlay_frame_count_ / seconds;
          overlay_frame_count_ = 0;
          overlay_sample_start_ = now;
        }
        UpdateStatusOverlay();
        const HRESULT hr = renderer_.RenderFrame(
            frame.format, frame.pixels.data(), frame.width, frame.height,
            frame.stride);
        if (FAILED(hr)) Logger::Instance().Error(L"Video frame rendering failed", hr);
      }
      return 0;
    }
    case kCaptureErrorMessage: {
      const HRESULT hr = capture_.LastError();
      audio_.Stop();
      capture_.Stop();
      viewer_mode_ = false;
      renderer_ = {};
      const std::wstring guidance =
          hr == HRESULT_FROM_WIN32(ERROR_TIMEOUT)
              ? L"The device stopped delivering video frames. Select another "
                L"format or reconnect the device before trying again."
              : L"The device may be disconnected or already in use.";
      MessageBoxW(window_,
          (L"Video capture stopped.\n\n" + HResultText(hr) +
           L"\n\n" + guidance).c_str(),
          L"Failed to capture video", MB_ICONERROR);
      RecreateSetupWindow();
      return 0;
    }
    case kAudioErrorMessage: {
      const HRESULT hr = audio_.LastError();
      audio_.Stop();
      capture_.Stop();
      viewer_mode_ = false;
      renderer_ = {};
      MessageBoxW(window_,
          (L"Audio monitoring stopped.\n\n" + HResultText(hr) +
           L"\n\nThe selected input and output may use incompatible formats.").c_str(),
          L"Failed to monitor audio", MB_ICONERROR);
      RecreateSetupWindow();
      return 0;
    }
    case WM_KEYDOWN:
      if (wparam == VK_F11 || (wparam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) ToggleFullscreen();
      else if (wparam == VK_ESCAPE && fullscreen_) ToggleFullscreen();
      else if (wparam == 'B') SetBorderless(!settings_.borderless);
      else if (wparam == 'T') SendMessageW(window_, WM_COMMAND, kMenuTopmost, 0);
      else if (wparam == 'M') {
        settings_.muted = !settings_.muted;
        audio_.SetMuted(settings_.muted);
      }
      else if (wparam == 'I') {
        settings_.status_overlay = !settings_.status_overlay;
        UpdateStatusOverlay();
        SaveSettings(settings_path_, settings_);
      }
      return 0;
    case WM_CONTEXTMENU: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (point.x == -1) { RECT r{}; GetWindowRect(window_, &r); point = {r.left + 20, r.top + 20}; }
      ShowContextMenu(point); return 0;
    }
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      const UINT dpi = GetDpiForWindow(window_);
      info->ptMinTrackSize = {MulDiv(320, dpi, 96), MulDiv(240, dpi, 96)};
      return 0;
    }
    case WM_SIZE:
      if (viewer_mode_ && wparam != SIZE_MINIMIZED) renderer_.Resize(LOWORD(lparam), HIWORD(lparam));
      return 0;
    case WM_ENTERSIZEMOVE:
      if (viewer_mode_ && !fullscreen_) GetWindowRect(window_, &size_move_start_rect_);
      return 0;
    case WM_EXITSIZEMOVE:
      if (viewer_mode_ && !fullscreen_ && !programmatic_resize_) {
        RECT rect{};
        if (GetWindowRect(window_, &rect)) {
          const LONG old_width = size_move_start_rect_.right - size_move_start_rect_.left;
          const LONG old_height = size_move_start_rect_.bottom - size_move_start_rect_.top;
          const LONG new_width = rect.right - rect.left;
          const LONG new_height = rect.bottom - rect.top;
          if (old_width != new_width || old_height != new_height) {
            settings_.window_scale_percent = 0;
          }
          settings_.window = rect;
        }
        SaveSettings(settings_path_, settings_);
      }
      return 0;
    case WM_TIMER:
      if (wparam == kRenderTimer) {
        // The shell has no incoming frames yet, so one clear/present is
        // sufficient. Continuous Present on the UI timer can monopolize the
        // message pump on some display drivers. Milestone 2 owns rendering on
        // a dedicated thread and wakes it only when a new frame is available.
        KillTimer(window_, kRenderTimer);
        Logger::Instance().Info(L"First render begin");
        renderer_.Render();
        Logger::Instance().Info(L"First render complete");
      }
      return 0;
    case WM_PAINT:
      if (!viewer_mode_) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window_, &paint);
        PaintSetup(dc);
        EndPaint(window_, &paint);
        return 0;
      }
      break;
    case WM_ERASEBKGND:
      if (viewer_mode_) return 1;
      {
        RECT client{};
        GetClientRect(window_, &client);
        HBRUSH brush = CreateSolidBrush(kSetupBackground);
        FillRect(reinterpret_cast<HDC>(wparam), &client, brush);
        DeleteObject(brush);
        return 1;
      }
    case WM_CLOSE: audio_.Stop(); capture_.Stop(); SaveState(); DestroyWindow(window); return 0;
    case WM_DESTROY:
      if (setup_font_) { DeleteObject(setup_font_); setup_font_ = nullptr; }
      if (setup_title_font_) { DeleteObject(setup_title_font_); setup_title_font_ = nullptr; }
      if (setup_small_font_) { DeleteObject(setup_small_font_); setup_small_font_ = nullptr; }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void App::CreateSetupControls() {
  for (HWND child = GetWindow(window_, GW_CHILD); child;) {
    HWND next = GetWindow(child, GW_HWNDNEXT); DestroyWindow(child); child = next;
  }
  if (setup_font_) { DeleteObject(setup_font_); setup_font_ = nullptr; }
  if (setup_title_font_) { DeleteObject(setup_title_font_); setup_title_font_ = nullptr; }
  if (setup_small_font_) { DeleteObject(setup_small_font_); setup_small_font_ = nullptr; }
  const UINT dpi = GetDpiForWindow(window_);
  const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
  NONCLIENTMETRICSW metrics{sizeof(metrics)};
  if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                                 &metrics, 0, dpi)) {
    setup_font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    LOGFONTW title_font = metrics.lfMessageFont;
    title_font.lfHeight = -MulDiv(20, dpi, 72);
    title_font.lfWeight = FW_SEMIBOLD;
    wcscpy_s(title_font.lfFaceName, L"Segoe UI Variable Display");
    setup_title_font_ = CreateFontIndirectW(&title_font);
    LOGFONTW small_font = metrics.lfMessageFont;
    small_font.lfHeight = -MulDiv(9, dpi, 72);
    wcscpy_s(small_font.lfFaceName, L"Segoe UI Variable Text");
    setup_small_font_ = CreateFontIndirectW(&small_font);
  }
  const HFONT font = setup_font_ ? setup_font_
                                 : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  auto static_text = [&](const wchar_t* text, int id, int x, int y, int width,
                         int height, HFONT control_font) {
    HWND control = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                 scale(x), scale(y), scale(width), scale(height),
                                 window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 nullptr, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(control_font), TRUE);
  };
  auto combo = [&](int id, int y) {
    HWND control = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        CBS_DROPDOWNLIST | WS_VSCROLL, scale(205), scale(y - 4),
        scale(370), scale(220), window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowTheme(control, L"Explorer", nullptr);
    return control;
  };
  static_text(L"Capture setup", kSetupTitle, 44, 24, 400, 35,
              setup_title_font_ ? setup_title_font_ : font);
  static_text(L"Choose the devices and video format to preview.",
              kSetupSubtitle, 44, 59, 500, 22,
              setup_small_font_ ? setup_small_font_ : font);
  static_text(L"Video device", 0, 64, 111, 128, 22, font);
  video_combo_ = combo(kVideoCombo, 111);
  static_text(L"Video format", 0, 64, 150, 128, 22, font);
  video_format_combo_ = combo(kVideoFormatCombo, 150);
  static_text(L"Audio input", 0, 64, 189, 128, 22, font);
  audio_in_combo_ = combo(kAudioInCombo, 189);
  static_text(L"Audio output", 0, 64, 228, 128, 22, font);
  audio_out_combo_ = combo(kAudioOutCombo, 228);
  static_text(L"Audio is monitored directly to the selected output.",
              kSetupHelper, 205, 260, 370, 20,
              setup_small_font_ ? setup_small_font_ : font);
  PopulateCombo(video_combo_, videos_, settings_.video_device_id);
  RefreshVideoFormats();
  PopulateCombo(audio_in_combo_, audio_inputs_, settings_.audio_input_id);
  PopulateCombo(audio_out_combo_, audio_outputs_, settings_.audio_output_id);
  start_button_ = CreateWindowW(L"BUTTON", L"\x25B6  Start capture",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      scale(452), scale(307), scale(150), scale(40), window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStart)), nullptr, nullptr);
  SendMessageW(start_button_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  refresh_button_ = CreateWindowW(
      L"BUTTON", L"\x21BB  Refresh devices",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      scale(294), scale(307), scale(150), scale(40), window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshDevices)),
      nullptr, nullptr);
  SendMessageW(refresh_button_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  if (videos_.empty() || video_formats_.empty()) {
    EnableWindow(start_button_, FALSE);
    static_text(videos_.empty() ? L"No video capture device found."
                                : L"No usable video format found.",
                kSetupNotice, 64, 282, 360, 20,
                setup_small_font_ ? setup_small_font_ : font);
  }
  InvalidateRect(window_, nullptr, TRUE);
}

void App::PaintSetup(HDC dc) {
  RECT client{};
  GetClientRect(window_, &client);
  HBRUSH background = CreateSolidBrush(kSetupBackground);
  FillRect(dc, &client, background);
  DeleteObject(background);

  const UINT dpi = GetDpiForWindow(window_);
  const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
  RECT card{scale(40), scale(92), scale(600), scale(291)};
  HBRUSH card_brush = CreateSolidBrush(kSetupCard);
  HPEN border_pen = CreatePen(PS_SOLID, std::max(1, scale(1)), kSetupBorder);
  HGDIOBJ old_brush = SelectObject(dc, card_brush);
  HGDIOBJ old_pen = SelectObject(dc, border_pen);
  RoundRect(dc, card.left, card.top, card.right, card.bottom,
            scale(10), scale(10));
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(border_pen);
  DeleteObject(card_brush);
}

void App::DrawSetupButton(const DRAWITEMSTRUCT& item) {
  const bool primary = item.CtlID == kStart;
  const bool disabled = (item.itemState & ODS_DISABLED) != 0;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  COLORREF fill = primary ? (pressed ? kAccentPressed : kAccent)
                          : (pressed ? RGB(244, 247, 250) : kSetupCard);
  COLORREF border = primary ? fill : (pressed ? RGB(180, 190, 202) : kSetupBorder);
  COLORREF text = primary ? RGB(255, 255, 255) : kSetupText;
  if (disabled) {
    fill = RGB(229, 233, 239);
    border = RGB(213, 219, 227);
    text = RGB(139, 149, 161);
  }
  HBRUSH canvas_brush = CreateSolidBrush(kSetupBackground);
  FillRect(item.hDC, &item.rcItem, canvas_brush);
  DeleteObject(canvas_brush);
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ old_brush = SelectObject(item.hDC, brush);
  HGDIOBJ old_pen = SelectObject(item.hDC, pen);
  const UINT dpi = GetDpiForWindow(window_);
  const int radius = MulDiv(8, dpi, 96);
  RoundRect(item.hDC, item.rcItem.left, item.rcItem.top,
            item.rcItem.right, item.rcItem.bottom, radius, radius);
  SelectObject(item.hDC, old_pen);
  SelectObject(item.hDC, old_brush);
  DeleteObject(pen);
  DeleteObject(brush);

  wchar_t caption[128]{};
  GetWindowTextW(item.hwndItem, caption, ARRAYSIZE(caption));
  SetBkMode(item.hDC, TRANSPARENT);
  SetTextColor(item.hDC, text);
  HGDIOBJ old_font = SelectObject(
      item.hDC, setup_font_ ? setup_font_
                            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
  RECT text_rect = item.rcItem;
  DrawTextW(item.hDC, caption, -1, &text_rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(item.hDC, old_font);
  if (item.itemState & ODS_FOCUS) {
    RECT focus = item.rcItem;
    InflateRect(&focus, -MulDiv(4, dpi, 96), -MulDiv(4, dpi, 96));
    DrawFocusRect(item.hDC, &focus);
  }
}

void App::RecreateSetupWindow() {
  RECT setup_rect{};
  if (fullscreen_) {
    setup_rect = restore_placement_.rcNormalPosition;
    fullscreen_ = false;
  } else {
    GetWindowRect(window_, &setup_rect);
  }
  HWND viewer_window = window_;
  const UINT setup_dpi = GetDpiForWindow(viewer_window);
  ShowWindow(viewer_window, SW_HIDE);
  viewer_mode_ = false;
  KillTimer(viewer_window, kRenderTimer);
  renderer_ = {};

  videos_ = EnumerateVideoDevices();
  audio_inputs_ = EnumerateAudioInputs();
  audio_outputs_ = EnumerateAudioOutputs();

  // A flip-model swap chain can remain associated with an HWND in DWM after
  // its COM objects are released. A fresh setup HWND guarantees a GDI surface.
  SetWindowLongPtrW(viewer_window, GWLP_USERDATA, 0);
  DestroyWindow(viewer_window);
  window_ = CreateWindowExW(
      0, kClassName, L"CaptureView", WS_OVERLAPPEDWINDOW,
      setup_rect.left, setup_rect.top,
      MulDiv(640, setup_dpi, 96), MulDiv(400, setup_dpi, 96),
      nullptr, nullptr, GetModuleHandleW(nullptr), this);
  if (!window_) {
    Logger::Instance().Error(L"Failed to recreate settings window",
                             HRESULT_FROM_WIN32(GetLastError()));
    PostQuitMessage(1);
    return;
  }
  const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                        sizeof(corner));
  ShowWindow(window_, SW_SHOW);
  UpdateWindow(window_);
  Logger::Instance().Info(L"Settings screen ready");
}

void App::PopulateCombo(HWND combo, const std::vector<DeviceInfo>& devices, const std::wstring& selected) {
  for (const auto& device : devices) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(device.name.c_str()));
  SelectSaved(combo, devices, selected);
}

void App::RefreshVideoFormats() {
  if (!video_format_combo_) return;
  SendMessageW(video_format_combo_, CB_RESETCONTENT, 0, 0);
  video_formats_.clear();
  const std::wstring device_id = SelectedId(video_combo_, videos_);
  if (!device_id.empty()) video_formats_ = EnumerateVideoFormats(device_id);

  int selected_index = -1;
  for (size_t index = 0; index < video_formats_.size(); ++index) {
    const auto& format = video_formats_[index];
    const std::wstring display = format.DisplayName();
    SendMessageW(video_format_combo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(display.c_str()));
    if (format.width == settings_.video_width &&
        format.height == settings_.video_height &&
        format.frame_rate_numerator == settings_.video_frame_rate_numerator &&
        format.frame_rate_denominator == settings_.video_frame_rate_denominator &&
        format.subtype_name == settings_.video_subtype) {
      selected_index = static_cast<int>(index);
    }
  }
  if (selected_index < 0 && !video_formats_.empty()) selected_index = 0;
  SendMessageW(video_format_combo_, CB_SETCURSEL, selected_index, 0);
  if (IsWindow(start_button_)) {
    EnableWindow(start_button_, !device_id.empty() && !video_formats_.empty());
  }
}

void App::RefreshDevices() {
  const std::wstring selected_video = SelectedId(video_combo_, videos_);
  const std::wstring selected_audio_input = SelectedId(audio_in_combo_, audio_inputs_);
  const std::wstring selected_audio_output = SelectedId(audio_out_combo_, audio_outputs_);
  const LRESULT format_index = SendMessageW(video_format_combo_, CB_GETCURSEL, 0, 0);
  if (format_index >= 0 && static_cast<size_t>(format_index) < video_formats_.size()) {
    const auto& format = video_formats_[format_index];
    settings_.video_width = format.width;
    settings_.video_height = format.height;
    settings_.video_frame_rate_numerator = format.frame_rate_numerator;
    settings_.video_frame_rate_denominator = format.frame_rate_denominator;
    settings_.video_subtype = format.subtype_name;
  }

  EnableWindow(refresh_button_, FALSE);
  EnableWindow(start_button_, FALSE);
  SetCursor(LoadCursorW(nullptr, IDC_WAIT));
  videos_ = EnumerateVideoDevices();
  audio_inputs_ = EnumerateAudioInputs();
  audio_outputs_ = EnumerateAudioOutputs();

  SendMessageW(video_combo_, CB_RESETCONTENT, 0, 0);
  SendMessageW(audio_in_combo_, CB_RESETCONTENT, 0, 0);
  SendMessageW(audio_out_combo_, CB_RESETCONTENT, 0, 0);
  PopulateCombo(video_combo_, videos_, selected_video);
  PopulateCombo(audio_in_combo_, audio_inputs_, selected_audio_input);
  PopulateCombo(audio_out_combo_, audio_outputs_, selected_audio_output);
  RefreshVideoFormats();
  EnableWindow(refresh_button_, TRUE);
  SetCursor(LoadCursorW(nullptr, IDC_ARROW));
  Logger::Instance().Info(std::format(
      L"Devices refreshed: video={}, audio inputs={}, audio outputs={}",
      videos_.size(), audio_inputs_.size(), audio_outputs_.size()));
}

void App::StartViewer() {
  Logger::Instance().Info(L"Starting viewer transition");
  settings_.video_device_id = SelectedId(video_combo_, videos_);
  const LRESULT format_index = SendMessageW(video_format_combo_, CB_GETCURSEL, 0, 0);
  settings_.audio_input_id = SelectedId(audio_in_combo_, audio_inputs_);
  settings_.audio_output_id = SelectedId(audio_out_combo_, audio_outputs_);
  if (settings_.video_device_id.empty()) {
    MessageBoxW(window_, L"Select a video capture device.", L"Video Device not found", MB_ICONWARNING); return;
  }
  if (format_index < 0 || static_cast<size_t>(format_index) >= video_formats_.size()) {
    MessageBoxW(window_, L"The selected device did not provide a usable video format.",
                L"Unsupported Video Format", MB_ICONWARNING);
    return;
  }
  const auto& format = video_formats_[format_index];
  const bool resolution_changed = settings_.video_width != format.width ||
                                  settings_.video_height != format.height;
  settings_.video_width = format.width;
  settings_.video_height = format.height;
  settings_.video_frame_rate_numerator = format.frame_rate_numerator;
  settings_.video_frame_rate_denominator = format.frame_rate_denominator;
  settings_.video_subtype = format.subtype_name;
  Logger::Instance().Info(std::format(L"Selected video format: {}", format.DisplayName()));
  if (!SaveSettings(settings_path_, settings_)) {
    Logger::Instance().Error(L"Failed to save settings", E_FAIL);
  }
  Logger::Instance().Info(L"Destroying setup controls");
  for (HWND child = GetWindow(window_, GW_CHILD); child;) {
    HWND next = GetWindow(child, GW_HWNDNEXT); DestroyWindow(child); child = next;
  }
  SetWindowTextW(window_, L"CaptureView");

  // Finalize the non-client area before creating the swap chain. Changing
  // styles after D3D initialization synchronously sends WM_SIZE and can
  // re-enter ResizeBuffers from inside SetWindowPos on some drivers.
  Logger::Instance().Info(L"Applying viewer window state");
  SetBorderless(settings_.borderless);
  SetWindowPos(window_, settings_.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
               0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
  if (resolution_changed) settings_.window_scale_percent = 100;
  if (settings_.window_scale_percent > 0) {
    ApplyViewerWindowSize(settings_.window_scale_percent);
  } else {
    programmatic_resize_ = true;
    SetWindowPos(window_, nullptr, settings_.window.left, settings_.window.top,
                 settings_.window.right - settings_.window.left,
                 settings_.window.bottom - settings_.window.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    programmatic_resize_ = false;
  }

  Logger::Instance().Info(L"Initializing Direct3D renderer");
  const HRESULT hr = renderer_.Initialize(window_);
  if (FAILED(hr)) {
    Logger::Instance().Error(L"Direct3D initialization failed", hr);
    MessageBoxW(window_, HResultText(hr).c_str(), L"Direct3D initialization failed", MB_ICONERROR);
    SetWindowTextW(window_, L"CaptureView");
    CreateSetupControls(); return;
  }
  renderer_.SetSourceSize(format.width, format.height);
  renderer_.SetFlip(settings_.flip_horizontal, settings_.flip_vertical);
  const VideoPixelFormat requested_format =
      format.subtype == MFVideoFormat_NV12
          ? VideoPixelFormat::Nv12
      : format.subtype == MFVideoFormat_YUY2 ? VideoPixelFormat::Yuy2
      : format.subtype == MFVideoFormat_MJPG ? VideoPixelFormat::Nv12
                                              : VideoPixelFormat::Bgra32;
  const bool native_yuv = requested_format != VideoPixelFormat::Bgra32 &&
                          renderer_.PrepareNativeYuv(
                              requested_format, format.width, format.height);
  capture_.PreferNativeYuv(native_yuv);
  const std::wstring native_path_name =
      format.subtype == MFVideoFormat_MJPG ? L"NV12 (MJPEG decode)"
                                           : format.subtype_name;
  const std::wstring renderer_path =
      format.subtype == MFVideoFormat_RGB24
          ? L"Renderer path: native RGB24 CPU expansion to BGRA32"
      : native_yuv ? std::format(L"Renderer path: D3D11 Video Processor {}",
                                 native_path_name)
                   : std::wstring(L"Renderer path: compatible RGB32");
  Logger::Instance().Info(renderer_path);
  const std::wstring video_name = [&] {
    const auto found = std::find_if(videos_.begin(), videos_.end(), [&](const DeviceInfo& device) {
      return device.id == settings_.video_device_id;
    });
    return found == videos_.end() ? std::wstring(L"Video") : found->name;
  }();
  overlay_video_line_ = std::format(L"{} | {}", video_name, format.DisplayName());
  overlay_frame_count_ = 0;
  overlay_last_received_frames_ = 0;
  measured_input_fps_ = 0.0;
  measured_fps_ = 0.0;
  overlay_sample_start_ = std::chrono::steady_clock::now();
  UpdateStatusOverlay();
  viewer_mode_ = true;
  SetTimer(window_, kRenderTimer, 16, nullptr);
  capture_.Start(settings_.video_device_id, format, window_,
                 kVideoFrameMessage, kCaptureErrorMessage);
  if (!settings_.audio_input_id.empty() && !settings_.audio_output_id.empty()) {
    if (!audio_.Start(settings_.audio_input_id, settings_.audio_output_id,
                      window_, kAudioErrorMessage, settings_.muted)) {
      Logger::Instance().Error(L"Failed to start audio monitoring thread",
                               audio_.LastError());
    }
  }
  Logger::Instance().Info(L"Viewer transition complete");
  SaveState();
}

void App::ApplyViewerWindowSize(int percent) {
  if (!viewer_mode_ && (settings_.video_width == 0 || settings_.video_height == 0)) return;
  RECT rect{0, 0,
            static_cast<LONG>(settings_.video_width * percent / 100),
            static_cast<LONG>(settings_.video_height * percent / 100)};
  const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
  const DWORD ex_style = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE));
  AdjustWindowRectExForDpi(&rect, style, FALSE, ex_style,
                           GetDpiForWindow(window_));
  programmatic_resize_ = true;
  SetWindowPos(window_, nullptr, 0, 0, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  programmatic_resize_ = false;
}

void App::SetWindowScale(int percent) {
  if (!viewer_mode_ || fullscreen_) return;
  settings_.window_scale_percent = percent;
  ApplyViewerWindowSize(percent);
  RECT rect{};
  if (GetWindowRect(window_, &rect)) settings_.window = rect;
  SaveSettings(settings_path_, settings_);
}

void App::UpdateStatusOverlay() {
  if (!settings_.status_overlay) {
    renderer_.SetOverlay(false, {});
    return;
  }
  std::wstring audio_status = L"Audio: off";
  if (audio_.IsRunning()) {
    audio_status = std::format(L"Audio: {} kHz {}ch | Queue: {} frames{}",
        audio_.SampleRate() / 1000.0, audio_.Channels(), audio_.QueuedFrames(),
        settings_.muted ? L" | Muted" : L"");
  }
  renderer_.SetOverlay(settings_.status_overlay,
      std::format(L"{}\nInput: {:.1f} fps | Render: {:.1f} fps | Dropped: {} | Video queue: {}\n{}",
                  overlay_video_line_, measured_input_fps_, measured_fps_,
                  capture_.DroppedFrames(), capture_.QueueDepth(), audio_status));
}

void App::SetBorderless(bool enabled) {
  settings_.borderless = enabled;
  // SetWindowLongPtr replaces the complete style value. Preserve WS_VISIBLE;
  // otherwise DWM can keep showing a stale thumbnail of a now-hidden window,
  // which looks frozen even though its message pump is still responsive.
  SetWindowLongPtrW(window_, GWL_STYLE,
                    (enabled ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_VISIBLE);
  SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  if (viewer_mode_ && !fullscreen_ && settings_.window_scale_percent > 0) {
    ApplyViewerWindowSize(settings_.window_scale_percent);
  }
}

void App::ToggleFullscreen() {
  if (!viewer_mode_) return;
  fullscreen_ = !fullscreen_;
  if (fullscreen_) {
    GetWindowPlacement(window_, &restore_placement_);
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &info);
    SetWindowLongPtrW(window_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(window_, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                 info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
                 SWP_FRAMECHANGED);
  } else {
    SetWindowLongPtrW(window_, GWL_STYLE,
                      (settings_.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW) |
                          WS_VISIBLE);
    SetWindowPlacement(window_, &restore_placement_);
    SetWindowPos(window_, settings_.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
  }
}

void App::ShowContextMenu(POINT point) {
  HMENU menu = CreatePopupMenu();
  HMENU scale_menu = CreatePopupMenu();
  HMENU flip_menu = CreatePopupMenu();
  const auto scale_item = [&](int id, int percent) {
    AppendMenuW(scale_menu,
                MF_STRING | (settings_.window_scale_percent == percent ? MF_CHECKED : 0) |
                    (fullscreen_ ? MF_GRAYED : 0),
                id, std::format(L"{}%", percent).c_str());
  };
  scale_item(kMenuScale150, 150);
  scale_item(kMenuScale125, 125);
  scale_item(kMenuScale100, 100);
  scale_item(kMenuScale75, 75);
  scale_item(kMenuScale50, 50);
  if (settings_.window_scale_percent == 0) {
    AppendMenuW(scale_menu, MF_STRING | MF_CHECKED | MF_GRAYED, 0, L"Custom");
  }
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scale_menu), L"Window Size");
  AppendMenuW(menu, MF_STRING, kMenuFullscreen, L"Fullscreen\tF11");
  AppendMenuW(menu, MF_STRING | (settings_.borderless ? MF_CHECKED : 0), kMenuBorderless, L"Borderless\tB");
  AppendMenuW(menu, MF_STRING | (settings_.always_on_top ? MF_CHECKED : 0), kMenuTopmost, L"Always on Top\tT");
  AppendMenuW(menu, MF_STRING | (settings_.muted ? MF_CHECKED : 0), kMenuMute, L"Mute\tM");
  AppendMenuW(menu, MF_STRING | (settings_.status_overlay ? MF_CHECKED : 0),
              kMenuOverlay, L"Status Overlay\tI");
  AppendMenuW(flip_menu,
              MF_STRING | (settings_.flip_horizontal ? MF_CHECKED : 0),
              kMenuFlipHorizontal, L"Horizontally");
  AppendMenuW(flip_menu,
              MF_STRING | (settings_.flip_vertical ? MF_CHECKED : 0),
              kMenuFlipVertical, L"Vertically");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(flip_menu), L"Flip");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
  AppendMenuW(menu, MF_STRING, kMenuAbout, L"About CaptureView");
  AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr); DestroyMenu(menu);
}

void App::ShowAbout() {
  const std::wstring instruction = std::format(L"CaptureView {}", kVersion);
  const std::wstring content = std::format(
      L"A lightweight Windows USB capture viewer.\n\n"
      L"Version: {}\n"
      L"Created by hine / Garbage design Works\n\n"
      L"Project and support:\n"
      L"<a href=\"{}\">github.com/hine/capture-viewer</a>",
      kVersion, kRepositoryUrl);
  const std::wstring license = std::format(
      L"License\nSource code and documentation: "
      L"<a href=\"{}\">MIT License</a>.\n"
      L"Copyright (c) 2026 hine.\n"
      L"CaptureView name and logo are reserved brand assets.\n"
      L"Third-party notices: None registered for this build.",
      kLicenseUrl);

  TASKDIALOGCONFIG config{sizeof(config)};
  config.hwndParent = window_;
  config.hInstance = GetModuleHandleW(nullptr);
  config.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION |
                   TDF_SIZE_TO_CONTENT;
  config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
  config.pszWindowTitle = L"About CaptureView";
  config.pszMainIcon = MAKEINTRESOURCEW(IDI_CAPTUREVIEW);
  config.pszMainInstruction = instruction.c_str();
  config.pszContent = content.c_str();
  config.pszFooter = license.c_str();
  config.pfCallback = AboutDialogCallback;
  TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void App::SaveState() {
  if (viewer_mode_ && !fullscreen_) {
    RECT rect{}; if (GetWindowRect(window_, &rect)) settings_.window = rect;
  }
  SaveSettings(settings_path_, settings_);
}
}
