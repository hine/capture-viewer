#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <windows.h>

namespace cv {

class AudioPipeline {
 public:
  AudioPipeline() = default;
  ~AudioPipeline();
  AudioPipeline(const AudioPipeline&) = delete;
  AudioPipeline& operator=(const AudioPipeline&) = delete;

  bool Start(const std::wstring& input_id, const std::wstring& output_id,
             HWND notify_window, UINT error_message, bool muted);
  void Stop();
  void SetMuted(bool muted) { muted_ = muted; }
  HRESULT LastError() const { return last_error_.load(); }
  unsigned SampleRate() const { return sample_rate_.load(); }
  unsigned Channels() const { return channels_.load(); }
  unsigned QueuedFrames() const { return queued_frames_.load(); }
  bool IsRunning() const { return running_.load(); }

 private:
  void AudioLoop(std::wstring input_id, std::wstring output_id);
  void PublishError(HRESULT error);

  HWND notify_window_ = nullptr;
  UINT error_message_ = 0;
  HANDLE stop_event_ = nullptr;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> muted_{false};
  std::atomic<HRESULT> last_error_{S_OK};
  std::atomic<unsigned> sample_rate_{0};
  std::atomic<unsigned> channels_{0};
  std::atomic<unsigned> queued_frames_{0};
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace cv
