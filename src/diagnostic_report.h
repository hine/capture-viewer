#pragma once

#include "device_enumerator.h"
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace cv {

struct DiagnosticReportData {
  std::wstring app_version;
  bool packaged = false;
  bool viewer_active = false;
  std::wstring graphics_adapter;
  std::wstring renderer_path;
  HRESULT renderer_error = S_OK;
  std::wstring video_device;
  std::wstring selected_video_format;
  std::wstring native_media_type;
  std::wstring negotiated_media_type;
  std::vector<VideoFormatInfo> supported_video_formats;
  double input_fps = 0.0;
  double render_fps = 0.0;
  std::uint64_t received_frames = 0;
  std::uint64_t dropped_frames = 0;
  unsigned video_queue_depth = 0;
  HRESULT video_error = S_OK;
  std::wstring audio_input;
  std::wstring audio_output;
  bool audio_running = false;
  unsigned audio_sample_rate = 0;
  unsigned audio_channels = 0;
  unsigned audio_queue_frames = 0;
  HRESULT audio_error = S_OK;
  bool muted = false;
  bool flip_horizontal = false;
  bool flip_vertical = false;
};

std::wstring BuildDiagnosticReport(const DiagnosticReportData& data);
bool CopyTextToClipboard(HWND owner, const std::wstring& text);
void ShowDiagnosticReportDialog(HWND owner, const std::wstring& report);

}  // namespace cv
