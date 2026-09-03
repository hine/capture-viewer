#include "capture_pipeline.h"
#include "logger.h"
#include <cstdlib>
#include <format>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace cv {
class SourceReaderCallback final : public IMFSourceReaderCallback {
 public:
  explicit SourceReaderCallback(CapturePipeline* owner) : owner_(owner) {}
  STDMETHODIMP QueryInterface(REFIID id, void** object) override {
    if (!object) return E_POINTER;
    if (id == __uuidof(IUnknown) || id == __uuidof(IMFSourceReaderCallback)) {
      *object = static_cast<IMFSourceReaderCallback*>(this);
      AddRef(); return S_OK;
    }
    *object = nullptr; return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = --references_;
    if (!remaining) delete this;
    return remaining;
  }
  STDMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG,
                            IMFSample* sample) override {
    return owner_->HandleReadSample(status, flags, sample);
  }
  STDMETHODIMP OnFlush(DWORD) override { return S_OK; }
  STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
 private:
  std::atomic<ULONG> references_{1};
  CapturePipeline* owner_;
};

namespace {

std::wstring VideoSubtypeName(const GUID& subtype) {
  if (subtype == MFVideoFormat_NV12) return L"NV12";
  if (subtype == MFVideoFormat_YUY2) return L"YUY2";
  if (subtype == MFVideoFormat_MJPG) return L"MJPEG";
  if (subtype == MFVideoFormat_RGB32) return L"RGB32";
  return L"other";
}

std::wstring MediaTypeSummary(IMFMediaType* type) {
  if (!type) return L"unavailable";
  GUID subtype{};
  UINT32 width = 0, height = 0;
  UINT32 rate_numerator = 0, rate_denominator = 0;
  UINT32 aspect_numerator = 0, aspect_denominator = 0;
  UINT32 interlace = MFVideoInterlace_Unknown;
  UINT32 stride_value = 0, sample_size = 0;
  type->GetGUID(MF_MT_SUBTYPE, &subtype);
  MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
  MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &rate_numerator,
                      &rate_denominator);
  MFGetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, &aspect_numerator,
                      &aspect_denominator);
  type->GetUINT32(MF_MT_INTERLACE_MODE, &interlace);
  type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride_value);
  type->GetUINT32(MF_MT_SAMPLE_SIZE, &sample_size);
  return std::format(
      L"{} {}x{} {}/{} fps, aspect {}/{}, interlace {}, stride {}, sample {}",
      VideoSubtypeName(subtype), width, height, rate_numerator,
      rate_denominator, aspect_numerator, aspect_denominator, interlace,
      static_cast<LONG>(stride_value), sample_size);
}

HRESULT OpenVideoSource(const std::wstring& device_id,
                        ComPtr<IMFMediaSource>& source) {
  ComPtr<IMFAttributes> attributes;
  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  HRESULT hr = MFCreateAttributes(&attributes, 1);
  if (SUCCEEDED(hr)) {
    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  }
  if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attributes.Get(), &devices, &count);
  if (FAILED(hr)) return hr;

  HRESULT open_result = MF_E_NOT_FOUND;
  for (UINT32 index = 0; index < count; ++index) {
    wchar_t* id = nullptr;
    UINT32 id_length = 0;
    if (SUCCEEDED(devices[index]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &id, &id_length)) &&
        id && device_id == id) {
      open_result = devices[index]->ActivateObject(IID_PPV_ARGS(&source));
    }
    CoTaskMemFree(id);
    devices[index]->Release();
  }
  CoTaskMemFree(devices);
  return source ? S_OK : open_result;
}

}  // namespace

CapturePipeline::~CapturePipeline() { Stop(); }

bool CapturePipeline::Start(const std::wstring& device_id,
                            const VideoFormatInfo& format,
                            HWND notify_window, UINT frame_message,
                            UINT error_message) {
  Stop();
  notify_window_ = notify_window;
  frame_message_ = frame_message;
  error_message_ = error_message;
  stopping_ = false;
  frame_notification_pending_ = false;
  last_error_ = S_OK;
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stop_event_) {
    last_error_ = HRESULT_FROM_WIN32(GetLastError());
    return false;
  }
  thread_ = std::thread(&CapturePipeline::CaptureLoop, this, device_id, format);
  return true;
}

void CapturePipeline::Stop() {
  stopping_ = true;
  Logger::Instance().Info(L"Stopping video capture");
  if (stop_event_) SetEvent(stop_event_);
  if (thread_.joinable()) thread_.join();
  {
    std::scoped_lock lock(reader_mutex_);
    reader_.Reset();
    source_.Reset();
  }
  if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
  frame_notification_pending_ = false;
}

bool CapturePipeline::TakeLatestFrame(VideoFrame& frame) {
  std::scoped_lock lock(frame_mutex_);
  if (latest_frame_.pixels.empty()) {
    frame_notification_pending_ = false;
    return false;
  }
  frame = std::move(latest_frame_);
  latest_frame_ = {};
  frame_notification_pending_ = false;
  return true;
}

void CapturePipeline::PublishError(HRESULT error) {
  last_error_ = error;
  Logger::Instance().Error(L"Video capture failed", error);
  if (!stopping_.exchange(true) && notify_window_) {
    PostMessageW(notify_window_, error_message_, 0, 0);
  }
  if (stop_event_) SetEvent(stop_event_);
}

void CapturePipeline::CaptureLoop(std::wstring device_id,
                                  VideoFormatInfo format) {
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninitialize_com = SUCCEEDED(com_result);
  if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
    PublishError(com_result);
    return;
  }

  ComPtr<IMFMediaSource> source;
  HRESULT hr = OpenVideoSource(device_id, source);
  if (SUCCEEDED(hr)) {
    std::scoped_lock lock(reader_mutex_);
    source_ = source;
  }
  ComPtr<IMFSourceReader> reader;
  ComPtr<IMFAttributes> attributes;
  ComPtr<IMFSourceReaderCallback> callback;
  callback.Attach(new SourceReaderCallback(this));
  if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attributes, 3);
  if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
  if (SUCCEEDED(hr)) {
    hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
  }
  if (SUCCEEDED(hr)) {
    hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get());
  }
  if (SUCCEEDED(hr)) {
    hr = MFCreateSourceReaderFromMediaSource(source.Get(), attributes.Get(), &reader);
  }

  const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
  ComPtr<IMFMediaType> native_type;
  if (SUCCEEDED(hr)) {
    hr = reader->GetNativeMediaType(stream, format.native_index, &native_type);
  }
  if (SUCCEEDED(hr)) {
    Logger::Instance().Info(
        L"Video native media type: " + MediaTypeSummary(native_type.Get()));
  }
  if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(stream, nullptr, native_type.Get());

  ComPtr<IMFMediaType> output_type;
  if (SUCCEEDED(hr)) hr = MFCreateMediaType(&output_type);
  if (SUCCEEDED(hr)) hr = output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  const bool request_native_yuv =
      prefer_native_yuv_ && (format.subtype == MFVideoFormat_NV12 ||
                             format.subtype == MFVideoFormat_YUY2 ||
                             format.subtype == MFVideoFormat_MJPG);
  if (SUCCEEDED(hr)) {
    const GUID& output_subtype =
        request_native_yuv
            ? (format.subtype == MFVideoFormat_MJPG ? MFVideoFormat_NV12
                                                     : format.subtype)
            : MFVideoFormat_RGB32;
    hr = output_type->SetGUID(MF_MT_SUBTYPE, output_subtype);
  }
  if (SUCCEEDED(hr)) {
    hr = MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE,
                            format.width, format.height);
  }
  if (SUCCEEDED(hr)) {
    hr = MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE,
                             format.frame_rate_numerator,
                             format.frame_rate_denominator);
  }
  if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(stream, nullptr, output_type.Get());
  if (FAILED(hr) && request_native_yuv) {
    Logger::Instance().Error(
        L"Native YUV output negotiation failed; using RGB32 fallback", hr);
    hr = output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) {
      hr = reader->SetCurrentMediaType(stream, nullptr, output_type.Get());
    }
  }
  ComPtr<IMFMediaType> negotiated_output_type;
  if (SUCCEEDED(hr)) {
    hr = reader->GetCurrentMediaType(stream, &negotiated_output_type);
  }
  if (SUCCEEDED(hr)) {
    Logger::Instance().Info(L"Video negotiated output type: " +
                            MediaTypeSummary(negotiated_output_type.Get()));
  }
  if (FAILED(hr)) {
    if (source) source->Shutdown();
    PublishError(hr);
    if (uninitialize_com) CoUninitialize();
    return;
  }

  {
    GUID negotiated_subtype{};
    UINT32 negotiated_stride = 0;
    negotiated_output_type->GetGUID(MF_MT_SUBTYPE, &negotiated_subtype);
    negotiated_output_type->GetUINT32(MF_MT_DEFAULT_STRIDE,
                                      &negotiated_stride);
    std::scoped_lock lock(reader_mutex_);
    reader_ = reader;
    active_format_ = format;
    output_format_ = negotiated_subtype == MFVideoFormat_NV12
                         ? VideoPixelFormat::Nv12
                     : negotiated_subtype == MFVideoFormat_YUY2
                         ? VideoPixelFormat::Yuy2
                         : VideoPixelFormat::Bgra32;
    output_stride_ = negotiated_stride
                         ? static_cast<unsigned>(
                               std::abs(static_cast<LONG>(negotiated_stride)))
                         : format.width *
                               (output_format_ == VideoPixelFormat::Nv12
                                    ? 1
                                : output_format_ == VideoPixelFormat::Yuy2 ? 2
                                                                          : 4);
  }
  Logger::Instance().Info(std::format(L"Video capture started: {}", format.DisplayName()));
  hr = reader->ReadSample(stream, 0, nullptr, nullptr, nullptr, nullptr);
  if (FAILED(hr)) PublishError(hr);
  WaitForSingleObject(stop_event_, INFINITE);
  reader->Flush(stream);

  {
    std::scoped_lock lock(reader_mutex_);
    reader_.Reset();
    source_.Reset();
  }
  reader.Reset();
  if (source) source->Shutdown();
  Logger::Instance().Info(L"Video capture stopped");
  if (uninitialize_com) CoUninitialize();
}

HRESULT CapturePipeline::HandleReadSample(HRESULT status, DWORD flags,
                                          IMFSample* sample) {
  if (stopping_) return S_OK;
  if (FAILED(status)) { PublishError(status); return S_OK; }
  if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) {
    PublishError(MF_E_END_OF_STREAM); return S_OK;
  }

  HRESULT hr = S_OK;
  if (sample) {
    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    BYTE* data = nullptr;
    DWORD length = 0;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, &length);
    const unsigned stride = output_stride_;
    const size_t luma_or_rgb_bytes =
        static_cast<size_t>(stride) * active_format_.height;
    const size_t required =
        luma_or_rgb_bytes +
        (output_format_ == VideoPixelFormat::Nv12 ? luma_or_rgb_bytes / 2 : 0);
    if (SUCCEEDED(hr) && length >= required) {
      VideoFrame frame;
      frame.format = output_format_;
      frame.width = active_format_.width;
      frame.height = active_format_.height;
      frame.stride = stride;
      frame.pixels.assign(data, data + required);
      {
        std::scoped_lock lock(frame_mutex_);
        latest_frame_ = std::move(frame);
      }
      if (!frame_notification_pending_.exchange(true) && notify_window_) {
        PostMessageW(notify_window_, frame_message_, 0, 0);
      }
    }
    if (data) buffer->Unlock();
  }
  if (FAILED(hr)) { PublishError(hr); return S_OK; }

  ComPtr<IMFSourceReader> reader;
  {
    std::scoped_lock lock(reader_mutex_);
    reader = reader_;
  }
  if (!stopping_ && reader) {
    hr = reader->ReadSample(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
        0, nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) PublishError(hr);
  }
  return S_OK;
}

}  // namespace cv
