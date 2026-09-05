#pragma once
#include <filesystem>
#include <string>
#include <windows.h>
namespace cv {
struct Settings {
  int version = 1;
  std::wstring video_device_id;
  unsigned video_width = 0;
  unsigned video_height = 0;
  unsigned video_frame_rate_numerator = 0;
  unsigned video_frame_rate_denominator = 1;
  std::wstring video_subtype;
  std::wstring audio_input_id;
  std::wstring audio_output_id;
  RECT window{100, 100, 1060, 640};
  bool borderless = false;
  bool always_on_top = false;
  bool muted = false;
  bool status_overlay = false;
  bool flip_horizontal = false;
  bool flip_vertical = false;
  int window_scale_percent = 100;  // 0 means a manually resized custom size.
};
std::filesystem::path AppDataRoot();
Settings LoadSettings(const std::filesystem::path& path);
bool SaveSettings(const std::filesystem::path& path, const Settings& settings);
}
