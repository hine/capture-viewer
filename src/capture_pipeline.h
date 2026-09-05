#pragma once

#include "device_enumerator.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

namespace cv {

enum class VideoPixelFormat { Bgr24, Bgra32, Nv12, Yuy2 };

struct VideoFrame {
  VideoPixelFormat format = VideoPixelFormat::Bgra32;
  unsigned width = 0;
  unsigned height = 0;
  unsigned stride = 0;
  std::vector<std::uint8_t> pixels;
};

class SourceReaderCallback;

class CapturePipeline {
 public:
  CapturePipeline() = default;
  ~CapturePipeline();
  CapturePipeline(const CapturePipeline&) = delete;
  CapturePipeline& operator=(const CapturePipeline&) = delete;

  bool Start(const std::wstring& device_id, const VideoFormatInfo& format,
             HWND notify_window, UINT frame_message, UINT error_message);
  void PreferNativeYuv(bool enabled) { prefer_native_yuv_ = enabled; }
  void Stop();
  bool TakeLatestFrame(VideoFrame& frame);
  HRESULT LastError() const { return last_error_.load(); }
  std::uint64_t ReceivedFrames() const { return received_frames_.load(); }
  std::uint64_t DroppedFrames() const { return dropped_frames_.load(); }
  unsigned QueueDepth() const { return queue_depth_.load(); }

 private:
  void CaptureLoop(std::wstring device_id, VideoFormatInfo format);
  void PublishError(HRESULT error);
  HRESULT HandleReadSample(HRESULT status, DWORD flags, IMFSample* sample);
  friend class SourceReaderCallback;

  HWND notify_window_ = nullptr;
  UINT frame_message_ = 0;
  UINT error_message_ = 0;
  HANDLE stop_event_ = nullptr;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> frame_notification_pending_{false};
  std::atomic<HRESULT> last_error_{S_OK};
  std::atomic<std::uint64_t> received_frames_{0};
  std::atomic<std::uint64_t> dropped_frames_{0};
  std::atomic<unsigned> queue_depth_{0};
  std::thread thread_;
  std::mutex frame_mutex_;
  VideoFrame latest_frame_;
  std::mutex reader_mutex_;
  Microsoft::WRL::ComPtr<IMFMediaSource> source_;
  Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
  VideoFormatInfo active_format_;
  VideoPixelFormat output_format_ = VideoPixelFormat::Bgra32;
  unsigned output_stride_ = 0;
  bool prefer_native_yuv_ = false;
};

}  // namespace cv
