#pragma once
#include <windows.h>
#include <audioclient.h>
#include <string>
namespace cv {
inline std::wstring HResultText(HRESULT hr) {
  switch (hr) {
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
      return L"The audio input and output do not support a common format.";
    case AUDCLNT_E_DEVICE_INVALIDATED:
      return L"The audio device was disconnected or disabled.";
    case AUDCLNT_E_DEVICE_IN_USE:
      return L"The audio device is already in exclusive use.";
    case AUDCLNT_E_SERVICE_NOT_RUNNING:
      return L"The Windows Audio service is not running.";
    default:
      break;
  }
  wchar_t* buffer = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, static_cast<DWORD>(hr), 0,
                 reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::wstring text = buffer ? buffer : L"Unknown error";
  if (buffer) LocalFree(buffer);
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
  return text;
}
}
