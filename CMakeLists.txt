cmake_minimum_required(VERSION 3.24)
project(CaptureView VERSION 0.9.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "CaptureView targets Windows 10/11 only")
endif()

add_executable(CaptureView WIN32
  src/CaptureView.manifest
  src/main.cpp
  src/app.cpp
  src/audio_pipeline.cpp
  src/capture_pipeline.cpp
  src/device_enumerator.cpp
  src/logger.cpp
  src/renderer.cpp
  src/resources.rc
  src/settings.cpp
)
target_compile_features(CaptureView PRIVATE cxx_std_20)
target_compile_definitions(CaptureView PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  CAPTUREVIEW_VERSION="${PROJECT_VERSION}"
)
target_include_directories(CaptureView PRIVATE src)
target_link_libraries(CaptureView PRIVATE comctl32 d2d1 d3d11 d3dcompiler dwrite dwmapi dxgi mf mfplat mfreadwrite mfuuid mmdevapi ole32 propsys shell32 uxtheme)
if(MSVC)
  target_compile_options(CaptureView PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_options(CaptureView PRIVATE /SUBSYSTEM:WINDOWS)
endif()
