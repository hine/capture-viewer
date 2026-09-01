#pragma once
#include <guiddef.h>
#include <string>
#include <vector>
namespace cv {
struct DeviceInfo { std::wstring name; std::wstring id; };
struct VideoFormatInfo {
  unsigned native_index = 0;
  unsigned width = 0;
  unsigned height = 0;
  unsigned frame_rate_numerator = 0;
  unsigned frame_rate_denominator = 1;
  GUID subtype{};
  std::wstring subtype_name;

  std::wstring DisplayName() const;
};
std::vector<DeviceInfo> EnumerateVideoDevices();
std::vector<VideoFormatInfo> EnumerateVideoFormats(const std::wstring& device_id);
std::vector<DeviceInfo> EnumerateAudioInputs();
std::vector<DeviceInfo> EnumerateAudioOutputs();
}
