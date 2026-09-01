#include "app.h"
#include "common.h"
#include "logger.h"
#include <mfapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  // Keep the D3D client area in physical pixels. Without DPI awareness,
  // Windows bitmap-scales the completed swap chain on 125/150% displays,
  // which makes an otherwise 1:1 capture visibly soft.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com_hr)) {
    MessageBoxW(nullptr, cv::HResultText(com_hr).c_str(), L"COM initialization failed", MB_ICONERROR);
    return 1;
  }
  const auto root = cv::AppDataRoot();
  cv::Logger::Instance().Initialize(root);
  cv::Logger::Instance().Info(L"Application Start");
  const HRESULT mf_hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  if (FAILED(mf_hr)) {
    cv::Logger::Instance().Error(L"Media Foundation initialization failed", mf_hr);
    MessageBoxW(nullptr, cv::HResultText(mf_hr).c_str(), L"Media Foundation initialization failed", MB_ICONERROR);
    CoUninitialize(); return 1;
  }
  cv::App app;
  const int result = app.Run(instance, show_command);
  MFShutdown(); CoUninitialize();
  return result;
}
