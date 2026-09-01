#include "logger.h"
#include <chrono>
#include <format>
namespace cv {
Logger& Logger::Instance() { static Logger logger; return logger; }
bool Logger::Initialize(const std::filesystem::path& root) {
  std::error_code ec;
  std::filesystem::create_directories(root / L"logs", ec);
  stream_.open(root / L"logs" / L"captureview.log", std::ios::app);
  return stream_.is_open();
}
void Logger::Write(std::wstring_view level, std::wstring_view message) {
  std::scoped_lock lock(mutex_);
  if (!stream_) return;
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  stream_ << std::format(L"[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n", now, level, message);
  stream_.flush();
}
void Logger::Info(std::wstring_view message) { Write(L"INFO", message); }
void Logger::Error(std::wstring_view message, long result) {
  Write(L"ERROR", std::format(L"{} (HRESULT=0x{:08X})", message,
                               static_cast<unsigned long>(result)));
}
}
