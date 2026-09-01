#include "device_enumerator.h"
#include "logger.h"
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <format>
#include <tuple>
#include <utility>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace cv {
namespace {
std::wstring SubtypeName(const GUID& subtype) {
  if (subtype == MFVideoFormat_NV12) return L"NV12";
  if (subtype == MFVideoFormat_YUY2) return L"YUY2";
  if (subtype == MFVideoFormat_MJPG) return L"MJPEG";
  if (subtype == MFVideoFormat_RGB32) return L"RGB32";
  wchar_t text[64]{};
  StringFromGUID2(subtype, text, ARRAYSIZE(text));
  return text;
}

int SubtypeRank(const GUID& subtype) {
  if (subtype == MFVideoFormat_NV12) return 0;
  if (subtype == MFVideoFormat_YUY2) return 1;
  if (subtype == MFVideoFormat_MJPG) return 2;
  if (subtype == MFVideoFormat_RGB32) return 3;
  return 4;
}

int TargetRank(const VideoFormatInfo& format) {
  const double fps = format.frame_rate_denominator
                         ? static_cast<double>(format.frame_rate_numerator) /
                               format.frame_rate_denominator
                         : 0.0;
  if (format.width == 1920 && format.height == 1080 && fps >= 59.0) return 0;
  if (format.width == 1920 && format.height == 1080 && fps >= 29.0) return 1;
  if (format.width == 1280 && format.height == 720 && fps >= 59.0) return 2;
  return 3;
}
}

std::wstring VideoFormatInfo::DisplayName() const {
  const double fps = frame_rate_denominator
                         ? static_cast<double>(frame_rate_numerator) /
                               frame_rate_denominator
                         : 0.0;
  return std::format(L"{} x {} / {:.2f} fps / {}", width, height, fps,
                     subtype_name);
}

std::vector<DeviceInfo> EnumerateVideoDevices() {
  std::vector<DeviceInfo> result;
  ComPtr<IMFAttributes> attributes;
  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  HRESULT hr = MFCreateAttributes(&attributes, 1);
  if (SUCCEEDED(hr)) hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attributes.Get(), &devices, &count);
  if (FAILED(hr)) { Logger::Instance().Error(L"Video device enumeration failed", hr); return result; }
  for (UINT32 i = 0; i < count; ++i) {
    wchar_t* name = nullptr; wchar_t* id = nullptr; UINT32 name_len = 0, id_len = 0;
    devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &name_len);
    devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &id, &id_len);
    result.push_back({name ? name : L"Unnamed video device", id ? id : L""});
    CoTaskMemFree(name); CoTaskMemFree(id); devices[i]->Release();
  }
  CoTaskMemFree(devices);
  return result;
}

std::vector<VideoFormatInfo> EnumerateVideoFormats(const std::wstring& device_id) {
  std::vector<VideoFormatInfo> result;
  ComPtr<IMFAttributes> attributes;
  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  HRESULT hr = MFCreateAttributes(&attributes, 1);
  if (SUCCEEDED(hr)) hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attributes.Get(), &devices, &count);
  if (FAILED(hr)) {
    Logger::Instance().Error(L"Video format device lookup failed", hr);
    return result;
  }

  ComPtr<IMFMediaSource> source;
  for (UINT32 i = 0; i < count; ++i) {
    wchar_t* id = nullptr;
    UINT32 id_length = 0;
    if (SUCCEEDED(devices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &id, &id_length)) && id && device_id == id) {
      hr = devices[i]->ActivateObject(IID_PPV_ARGS(&source));
    }
    CoTaskMemFree(id);
    devices[i]->Release();
  }
  CoTaskMemFree(devices);
  if (!source) {
    if (SUCCEEDED(hr)) hr = MF_E_NOT_FOUND;
    Logger::Instance().Error(L"Selected video device could not be opened for format enumeration", hr);
    return result;
  }

  ComPtr<IMFSourceReader> reader;
  hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
  if (SUCCEEDED(hr)) {
    for (DWORD index = 0;; ++index) {
      ComPtr<IMFMediaType> type;
      hr = reader->GetNativeMediaType(
                                      static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                      index, &type);
      if (hr == MF_E_NO_MORE_TYPES) break;
      if (FAILED(hr)) {
        Logger::Instance().Error(L"Native video format enumeration failed", hr);
        break;
      }
      VideoFormatInfo format;
      format.native_index = index;
      if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE,
                                    &format.width, &format.height)) ||
          FAILED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE,
                                     &format.frame_rate_numerator,
                                     &format.frame_rate_denominator)) ||
          FAILED(type->GetGUID(MF_MT_SUBTYPE, &format.subtype))) {
        continue;
      }
      format.subtype_name = SubtypeName(format.subtype);
      result.push_back(std::move(format));
    }
  } else {
    Logger::Instance().Error(L"Failed to create source reader for format enumeration", hr);
  }
  reader.Reset();
  source->Shutdown();

  std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    const auto left_key = std::tuple(TargetRank(left), SubtypeRank(left.subtype),
                                     -static_cast<int>(left.width * left.height),
                                     -static_cast<int>(left.frame_rate_numerator /
                                                       std::max(1u, left.frame_rate_denominator)));
    const auto right_key = std::tuple(TargetRank(right), SubtypeRank(right.subtype),
                                      -static_cast<int>(right.width * right.height),
                                      -static_cast<int>(right.frame_rate_numerator /
                                                        std::max(1u, right.frame_rate_denominator)));
    return left_key < right_key;
  });
  Logger::Instance().Info(std::format(L"Video formats found: {}", result.size()));
  return result;
}

static std::vector<DeviceInfo> EnumerateAudio(EDataFlow flow) {
  std::vector<DeviceInfo> result;
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDeviceCollection> collection;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator));
  if (SUCCEEDED(hr)) hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
  if (FAILED(hr)) { Logger::Instance().Error(L"Audio device enumeration failed", hr); return result; }
  UINT count = 0; collection->GetCount(&count);
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device; ComPtr<IPropertyStore> properties;
    LPWSTR id = nullptr; PROPVARIANT name; PropVariantInit(&name);
    if (SUCCEEDED(collection->Item(i, &device)) && SUCCEEDED(device->GetId(&id)) &&
        SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
        SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name))) {
      result.push_back({name.vt == VT_LPWSTR ? name.pwszVal : L"Unnamed audio device", id});
    }
    PropVariantClear(&name); CoTaskMemFree(id);
  }
  return result;
}
std::vector<DeviceInfo> EnumerateAudioInputs() { return EnumerateAudio(eCapture); }
std::vector<DeviceInfo> EnumerateAudioOutputs() { return EnumerateAudio(eRender); }
}
