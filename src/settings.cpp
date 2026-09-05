#include "settings.h"
#include <fstream>
#include <regex>
#include <shlobj.h>
namespace cv {
namespace {
std::wstring Escape(std::wstring_view value) {
  std::wstring out;
  for (wchar_t c : value) { if (c == L'\\' || c == L'\"') out.push_back(L'\\'); out.push_back(c); }
  return out;
}
std::wstring StringValue(const std::wstring& text, const wchar_t* key) {
  std::wregex pattern(std::wstring(L"\\\"") + key + L"\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"");
  std::wsmatch match;
  if (!std::regex_search(text, match, pattern)) return {};
  std::wstring value = match[1];
  value = std::regex_replace(value, std::wregex(L"\\\\\\\""), L"\"");
  value = std::regex_replace(value, std::wregex(L"\\\\\\\\"), L"\\");
  return value;
}
int IntValue(const std::wstring& text, const wchar_t* key, int fallback) {
  std::wregex pattern(std::wstring(L"\\\"") + key + L"\\\"\\s*:\\s*(-?\\d+)");
  std::wsmatch match;
  return std::regex_search(text, match, pattern) ? std::stoi(match[1]) : fallback;
}
bool BoolValue(const std::wstring& text, const wchar_t* key, bool fallback) {
  std::wregex pattern(std::wstring(L"\\\"") + key + L"\\\"\\s*:\\s*(true|false)");
  std::wsmatch match;
  return std::regex_search(text, match, pattern) ? match[1] == L"true" : fallback;
}
}
std::filesystem::path AppDataRoot() {
  PWSTR value = nullptr;
  std::filesystem::path result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &value))) {
    result = std::filesystem::path(value) / L"CaptureView";
    CoTaskMemFree(value);
  }
  return result;
}
Settings LoadSettings(const std::filesystem::path& path) {
  Settings s;
  std::wifstream input(path);
  if (!input) return s;
  std::wstring text((std::istreambuf_iterator<wchar_t>(input)), {});
  s.version = IntValue(text, L"version", 1);
  s.video_device_id = StringValue(text, L"videoDeviceId");
  s.video_width = static_cast<unsigned>(IntValue(text, L"videoWidth", 0));
  s.video_height = static_cast<unsigned>(IntValue(text, L"videoHeight", 0));
  s.video_frame_rate_numerator = static_cast<unsigned>(IntValue(text, L"videoFrameRateNumerator", 0));
  s.video_frame_rate_denominator = static_cast<unsigned>(IntValue(text, L"videoFrameRateDenominator", 1));
  if (!s.video_frame_rate_denominator) s.video_frame_rate_denominator = 1;
  s.video_subtype = StringValue(text, L"videoSubtype");
  s.audio_input_id = StringValue(text, L"audioInputId");
  s.audio_output_id = StringValue(text, L"audioOutputId");
  s.window.left = IntValue(text, L"left", s.window.left);
  s.window.top = IntValue(text, L"top", s.window.top);
  s.window.right = s.window.left + IntValue(text, L"width", s.window.right - s.window.left);
  s.window.bottom = s.window.top + IntValue(text, L"height", s.window.bottom - s.window.top);
  s.borderless = BoolValue(text, L"borderless", false);
  s.always_on_top = BoolValue(text, L"alwaysOnTop", false);
  s.muted = BoolValue(text, L"muted", false);
  s.status_overlay = BoolValue(text, L"statusOverlay", false);
  s.flip_horizontal = BoolValue(text, L"flipHorizontal", false);
  s.flip_vertical = BoolValue(text, L"flipVertical", false);
  s.window_scale_percent = IntValue(text, L"windowScalePercent", 100);
  if (s.window_scale_percent != 0 && s.window_scale_percent != 50 &&
      s.window_scale_percent != 75 && s.window_scale_percent != 100 &&
      s.window_scale_percent != 125 && s.window_scale_percent != 150) {
    s.window_scale_percent = 100;
  }
  return s;
}
bool SaveSettings(const std::filesystem::path& path, const Settings& s) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::wofstream output(path, std::ios::trunc);
  if (!output) return false;
  output << L"{\n  \"version\": " << s.version
         << L",\n  \"videoDeviceId\": \"" << Escape(s.video_device_id)
         << L"\",\n  \"videoWidth\": " << s.video_width
         << L",\n  \"videoHeight\": " << s.video_height
         << L",\n  \"videoFrameRateNumerator\": " << s.video_frame_rate_numerator
         << L",\n  \"videoFrameRateDenominator\": " << s.video_frame_rate_denominator
         << L",\n  \"videoSubtype\": \"" << Escape(s.video_subtype)
         << L"\",\n  \"audioInputId\": \"" << Escape(s.audio_input_id)
         << L"\",\n  \"audioOutputId\": \"" << Escape(s.audio_output_id)
         << L"\",\n  \"window\": {\"left\": " << s.window.left << L", \"top\": " << s.window.top
         << L", \"width\": " << s.window.right - s.window.left << L", \"height\": "
         << s.window.bottom - s.window.top << L"},\n  \"borderless\": "
         << (s.borderless ? L"true" : L"false") << L",\n  \"alwaysOnTop\": "
         << (s.always_on_top ? L"true" : L"false") << L",\n  \"muted\": "
         << (s.muted ? L"true" : L"false") << L",\n  \"statusOverlay\": "
         << (s.status_overlay ? L"true" : L"false")
         << L",\n  \"flipHorizontal\": "
         << (s.flip_horizontal ? L"true" : L"false")
         << L",\n  \"flipVertical\": "
         << (s.flip_vertical ? L"true" : L"false")
         << L",\n  \"windowScalePercent\": " << s.window_scale_percent << L"\n}\n";
  return output.good();
}
}
