#include "audio_pipeline.h"
#include "logger.h"
#include <algorithm>
#include <audioclient.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace cv {
namespace {

HRESULT OpenEndpoint(IMMDeviceEnumerator* enumerator, const std::wstring& id,
                     ComPtr<IMMDevice>& device) {
  return id.empty() ? E_INVALIDARG : enumerator->GetDevice(id.c_str(), &device);
}

bool SupportsExactly(IAudioClient* client, const WAVEFORMATEX* format) {
  WAVEFORMATEX* closest = nullptr;
  const HRESULT result = client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                    format, &closest);
  CoTaskMemFree(closest);
  return result == S_OK;
}

WAVEFORMATEXTENSIBLE StandardStereoFormat(DWORD sample_rate, WORD bits,
                                          bool floating_point) {
  WAVEFORMATEXTENSIBLE format{};
  format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  format.Format.nChannels = 2;
  format.Format.nSamplesPerSec = sample_rate;
  format.Format.wBitsPerSample = bits;
  format.Format.nBlockAlign = static_cast<WORD>(format.Format.nChannels * bits / 8);
  format.Format.nAvgBytesPerSec = sample_rate * format.Format.nBlockAlign;
  format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.Samples.wValidBitsPerSample = bits;
  format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  format.SubFormat = floating_point ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
                                    : KSDATAFORMAT_SUBTYPE_PCM;
  return format;
}

}  // namespace

AudioPipeline::~AudioPipeline() { Stop(); }

bool AudioPipeline::Start(const std::wstring& input_id,
                          const std::wstring& output_id,
                          HWND notify_window, UINT error_message, bool muted) {
  Stop();
  notify_window_ = notify_window;
  error_message_ = error_message;
  muted_ = muted;
  stopping_ = false;
  last_error_ = S_OK;
  sample_rate_ = 0;
  channels_ = 0;
  queued_frames_ = 0;
  running_ = false;
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stop_event_) {
    last_error_ = HRESULT_FROM_WIN32(GetLastError());
    return false;
  }
  thread_ = std::thread(&AudioPipeline::AudioLoop, this, input_id, output_id);
  return true;
}

void AudioPipeline::Stop() {
  stopping_ = true;
  if (stop_event_) SetEvent(stop_event_);
  if (thread_.joinable()) thread_.join();
  if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
  running_ = false;
  queued_frames_ = 0;
}

void AudioPipeline::PublishError(HRESULT error) {
  last_error_ = error;
  Logger::Instance().Error(L"Audio monitoring failed", error);
  if (!stopping_ && notify_window_) PostMessageW(notify_window_, error_message_, 0, 0);
}

void AudioPipeline::AudioLoop(std::wstring input_id, std::wstring output_id) {
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninitialize_com = SUCCEEDED(com_result);
  if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
    PublishError(com_result); return;
  }

  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> input_device, output_device;
  ComPtr<IAudioClient> input_client, output_client;
  WAVEFORMATEX* input_format = nullptr;
  WAVEFORMATEX* output_format = nullptr;
  WAVEFORMATEX* shared_format = nullptr;
  WAVEFORMATEXTENSIBLE standard_formats[] = {
      StandardStereoFormat(48000, 32, true),
      StandardStereoFormat(48000, 16, false),
      StandardStereoFormat(44100, 32, true),
      StandardStereoFormat(44100, 16, false),
  };
  HANDLE input_event = nullptr, output_event = nullptr;

  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator));
  if (SUCCEEDED(hr)) hr = OpenEndpoint(enumerator.Get(), input_id, input_device);
  if (SUCCEEDED(hr)) hr = OpenEndpoint(enumerator.Get(), output_id, output_device);
  if (SUCCEEDED(hr)) hr = input_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                                  nullptr, &input_client);
  if (SUCCEEDED(hr)) hr = output_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                                   nullptr, &output_client);
  if (SUCCEEDED(hr)) hr = input_client->GetMixFormat(&input_format);
  if (SUCCEEDED(hr)) hr = output_client->GetMixFormat(&output_format);
  if (SUCCEEDED(hr) && SupportsExactly(output_client.Get(), input_format)) {
    shared_format = input_format;
  } else if (SUCCEEDED(hr) && SupportsExactly(input_client.Get(), output_format)) {
    shared_format = output_format;
  } else if (SUCCEEDED(hr)) {
    for (auto& candidate : standard_formats) {
      if (SupportsExactly(input_client.Get(), &candidate.Format) &&
          SupportsExactly(output_client.Get(), &candidate.Format)) {
        shared_format = &candidate.Format;
        break;
      }
    }
    // Shared-mode Audio Engine conversion is explicitly enabled below. Even
    // when IsFormatSupported does not report an exact match, 48 kHz stereo
    // float is the preferred interchange format for the converter.
    if (!shared_format) shared_format = &standard_formats[0].Format;
  }

  constexpr DWORD conversion_flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                     AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  constexpr DWORD input_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                conversion_flags;
  constexpr DWORD output_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                 AUDCLNT_STREAMFLAGS_NOPERSIST |
                                 conversion_flags;
  if (SUCCEEDED(hr)) {
    hr = input_client->Initialize(AUDCLNT_SHAREMODE_SHARED, input_flags, 0, 0,
                                  shared_format, nullptr);
  }
  if (SUCCEEDED(hr)) {
    hr = output_client->Initialize(AUDCLNT_SHAREMODE_SHARED, output_flags, 0, 0,
                                   shared_format, nullptr);
  }
  input_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  output_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (SUCCEEDED(hr) && (!input_event || !output_event)) {
    hr = HRESULT_FROM_WIN32(GetLastError());
  }
  if (SUCCEEDED(hr)) hr = input_client->SetEventHandle(input_event);
  if (SUCCEEDED(hr)) hr = output_client->SetEventHandle(output_event);

  ComPtr<IAudioCaptureClient> capture;
  ComPtr<IAudioRenderClient> render;
  UINT32 output_buffer_frames = 0;
  if (SUCCEEDED(hr)) hr = input_client->GetService(IID_PPV_ARGS(&capture));
  if (SUCCEEDED(hr)) hr = output_client->GetService(IID_PPV_ARGS(&render));
  if (SUCCEEDED(hr)) hr = output_client->GetBufferSize(&output_buffer_frames);
  if (SUCCEEDED(hr) && output_buffer_frames) {
    BYTE* initial_buffer = nullptr;
    hr = render->GetBuffer(output_buffer_frames, &initial_buffer);
    if (SUCCEEDED(hr)) {
      hr = render->ReleaseBuffer(output_buffer_frames,
                                 AUDCLNT_BUFFERFLAGS_SILENT);
    }
  }
  if (SUCCEEDED(hr)) hr = output_client->Start();
  if (SUCCEEDED(hr)) hr = input_client->Start();
  if (FAILED(hr)) {
    PublishError(hr);
  } else {
    sample_rate_ = shared_format->nSamplesPerSec;
    channels_ = shared_format->nChannels;
    running_ = true;
    Logger::Instance().Info(std::format(
        L"Audio monitoring started: {} Hz, {} channels, {} bits",
        shared_format->nSamplesPerSec, shared_format->nChannels,
        shared_format->wBitsPerSample));

    const size_t frame_bytes = shared_format->nBlockAlign;
    const size_t max_queue_frames = std::max<UINT32>(output_buffer_frames * 2, 512);
    std::deque<std::byte> queue;
    std::uint64_t captured_frames = 0;
    std::uint64_t captured_nonzero_bytes = 0;
    std::uint64_t rendered_frames = 0;
    auto next_statistics = std::chrono::steady_clock::now() +
                           std::chrono::seconds(2);
    HANDLE events[] = {stop_event_, input_event, output_event};
    bool running = true;
    while (running && !stopping_) {
      const DWORD wait = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE);
      if (wait == WAIT_OBJECT_0) break;
      if (wait == WAIT_OBJECT_0 + 1) {
        UINT32 packet_frames = 0;
        hr = capture->GetNextPacketSize(&packet_frames);
        while (SUCCEEDED(hr) && packet_frames) {
          BYTE* data = nullptr;
          UINT32 frames = 0;
          DWORD capture_flags = 0;
          hr = capture->GetBuffer(&data, &frames, &capture_flags, nullptr, nullptr);
          if (FAILED(hr)) break;
          const size_t bytes = static_cast<size_t>(frames) * frame_bytes;
          const bool silent = (capture_flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
          for (size_t i = 0; i < bytes; ++i) {
            const BYTE value = silent ? 0 : data[i];
            queue.push_back(static_cast<std::byte>(value));
            if (value) ++captured_nonzero_bytes;
          }
          captured_frames += frames;
          capture->ReleaseBuffer(frames);
          const size_t max_bytes = max_queue_frames * frame_bytes;
          while (queue.size() > max_bytes) queue.pop_front();
          queued_frames_ = static_cast<unsigned>(queue.size() / frame_bytes);
          hr = capture->GetNextPacketSize(&packet_frames);
        }
      } else if (wait == WAIT_OBJECT_0 + 2) {
        UINT32 padding = 0;
        hr = output_client->GetCurrentPadding(&padding);
        const UINT32 available = SUCCEEDED(hr) ? output_buffer_frames - padding : 0;
        if (SUCCEEDED(hr) && available) {
          BYTE* destination = nullptr;
          hr = render->GetBuffer(available, &destination);
          if (SUCCEEDED(hr)) {
            const size_t bytes = static_cast<size_t>(available) * frame_bytes;
            const bool muted = muted_.load();
            for (size_t i = 0; i < bytes; ++i) {
              if (queue.empty()) {
                destination[i] = 0;
              } else {
                const BYTE value = static_cast<BYTE>(queue.front());
                queue.pop_front();
                destination[i] = muted ? 0 : value;
              }
            }
            hr = render->ReleaseBuffer(available, 0);
            if (SUCCEEDED(hr)) rendered_frames += available;
            queued_frames_ = static_cast<unsigned>(queue.size() / frame_bytes);
          }
        }
      } else {
        hr = HRESULT_FROM_WIN32(GetLastError());
      }
      if (std::chrono::steady_clock::now() >= next_statistics) {
        Logger::Instance().Info(std::format(
            L"Audio stats: captured={} frames, nonzero={} bytes, rendered={} frames, queued={} frames",
            captured_frames, captured_nonzero_bytes, rendered_frames,
            queue.size() / frame_bytes));
        captured_frames = 0;
        captured_nonzero_bytes = 0;
        rendered_frames = 0;
        next_statistics = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
      }
      if (FAILED(hr)) { PublishError(hr); running = false; }
    }
    input_client->Stop();
    output_client->Stop();
    running_ = false;
    queued_frames_ = 0;
    Logger::Instance().Info(L"Audio monitoring stopped");
  }

  if (input_event) CloseHandle(input_event);
  if (output_event) CloseHandle(output_event);
  CoTaskMemFree(output_format);
  CoTaskMemFree(input_format);
  if (uninitialize_com) CoUninitialize();
}

}  // namespace cv
