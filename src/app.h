#pragma once
#include "audio_pipeline.h"
#include "capture_pipeline.h"
#include "device_enumerator.h"
#include "renderer.h"
#include "settings.h"
#include <chrono>
#include <cstdint>
#include <vector>
namespace cv {
class App {
 public:
  int Run(HINSTANCE instance, int show_command);
 private:
  static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
  bool CreateMainWindow(HINSTANCE, int);
  void CreateSetupControls();
  void PaintSetup(HDC dc);
  void DrawSetupButton(const DRAWITEMSTRUCT& item);
  void PopulateCombo(HWND combo, const std::vector<DeviceInfo>& devices, const std::wstring& selected);
  void RefreshVideoFormats();
  void RefreshDevices();
  void RecreateSetupWindow();
  void StartViewer();
  void ToggleFullscreen();
  void SetBorderless(bool enabled);
  void SetWindowScale(int percent);
  void ApplyViewerWindowSize(int percent);
  void UpdateStatusOverlay();
  void SaveState();
  void ShowContextMenu(POINT point);
  void ShowAbout();
  HWND window_ = nullptr;
  HWND video_combo_ = nullptr, video_format_combo_ = nullptr;
  HWND audio_in_combo_ = nullptr, audio_out_combo_ = nullptr;
  HWND start_button_ = nullptr, refresh_button_ = nullptr;
  HFONT setup_font_ = nullptr;
  HFONT setup_title_font_ = nullptr, setup_small_font_ = nullptr;
  std::vector<DeviceInfo> videos_, audio_inputs_, audio_outputs_;
  std::vector<VideoFormatInfo> video_formats_;
  AudioPipeline audio_;
  CapturePipeline capture_;
  Renderer renderer_;
  Settings settings_;
  std::filesystem::path settings_path_;
  bool viewer_mode_ = false, fullscreen_ = false;
  bool programmatic_resize_ = false;
  unsigned overlay_frame_count_ = 0;
  std::uint64_t overlay_last_received_frames_ = 0;
  double measured_input_fps_ = 0.0;
  double measured_fps_ = 0.0;
  std::chrono::steady_clock::time_point overlay_sample_start_{};
  std::wstring overlay_video_line_;
  RECT size_move_start_rect_{};
  WINDOWPLACEMENT restore_placement_{sizeof(WINDOWPLACEMENT)};
};
}
