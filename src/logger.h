#pragma once
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
namespace cv {
class Logger {
 public:
  static Logger& Instance();
  bool Initialize(const std::filesystem::path& root);
  void Info(std::wstring_view message);
  void Error(std::wstring_view message, long result);
 private:
  void Write(std::wstring_view level, std::wstring_view message);
  std::wofstream stream_;
  std::mutex mutex_;
};
}
