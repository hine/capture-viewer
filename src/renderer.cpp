#include "renderer.h"
#include "capture_pipeline.h"
#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
namespace cv {
namespace {
constexpr char kVertexShader[] = R"(
cbuffer Transform : register(b0) { float2 uvScale; float2 uvOffset; };
struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
  float2 positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
  float2 uvs[3] = { float2(0, 1), float2(0, -1), float2(2, 1) };
  Output output;
  output.position = float4(positions[id], 0, 1);
  output.uv = uvs[id] * uvScale + uvOffset;
  return output;
})";
constexpr char kPixelShader[] = R"(
Texture2D image : register(t0);
SamplerState imageSampler : register(s0);
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  return image.Sample(imageSampler, uv);
})";
}

HRESULT Renderer::Initialize(HWND window) {
  window_ = window;
  RECT rect{}; GetClientRect(window, &rect);
  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferCount = 2;
  desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = window;
  desc.SampleDesc.Count = 1;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL actual{};
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
      levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &desc, &swap_chain_, &device_, &actual, &context_);
#ifdef _DEBUG
  if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
    flags &= ~D3D11_CREATE_DEVICE_DEBUG;
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &desc, &swap_chain_, &device_, &actual, &context_);
  }
#endif
  if (SUCCEEDED(hr)) hr = CreateTarget();
  if (SUCCEEDED(hr)) hr = CreateShaders();
  if (SUCCEEDED(hr)) hr = device_.As(&video_device_);
  if (SUCCEEDED(hr)) hr = context_.As(&video_context_);
  if (SUCCEEDED(hr)) {
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           d2d_factory_.ReleaseAndGetAddressOf());
  }
  if (SUCCEEDED(hr)) hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = write_factory_->CreateTextFormat(
      L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"ja-JP", &overlay_format_);
  if (SUCCEEDED(hr)) hr = CreateOverlayResources();
  return hr;
}
HRESULT Renderer::CreateShaders() {
  Microsoft::WRL::ComPtr<ID3DBlob> vertex_code, pixel_code, errors;
  HRESULT hr = D3DCompile(kVertexShader, sizeof(kVertexShader), nullptr, nullptr,
                          nullptr, "main", "vs_4_0", 0, 0, &vertex_code, &errors);
  if (SUCCEEDED(hr)) {
    hr = D3DCompile(kPixelShader, sizeof(kPixelShader), nullptr, nullptr,
                    nullptr, "main", "ps_4_0", 0, 0, &pixel_code, &errors);
  }
  if (SUCCEEDED(hr)) hr = device_->CreateVertexShader(vertex_code->GetBufferPointer(),
      vertex_code->GetBufferSize(), nullptr, &vertex_shader_);
  if (SUCCEEDED(hr)) hr = device_->CreatePixelShader(pixel_code->GetBufferPointer(),
      pixel_code->GetBufferSize(), nullptr, &pixel_shader_);
  D3D11_SAMPLER_DESC desc{};
  desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  desc.MaxLOD = D3D11_FLOAT32_MAX;
  if (SUCCEEDED(hr)) hr = device_->CreateSamplerState(&desc, &sampler_);
  D3D11_BUFFER_DESC transform{};
  transform.ByteWidth = sizeof(float) * 4;
  transform.Usage = D3D11_USAGE_DEFAULT;
  transform.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (SUCCEEDED(hr)) {
    hr = device_->CreateBuffer(&transform, nullptr, &transform_buffer_);
  }
  if (SUCCEEDED(hr)) UpdateTransform();
  return hr;
}
void Renderer::SetFlip(bool horizontal, bool vertical) {
  flip_horizontal_ = horizontal;
  flip_vertical_ = vertical;
  UpdateTransform();
}
void Renderer::UpdateTransform() {
  if (!context_ || !transform_buffer_) return;
  const float transform[4] = {
      flip_horizontal_ ? -1.0f : 1.0f,
      flip_vertical_ ? -1.0f : 1.0f,
      flip_horizontal_ ? 1.0f : 0.0f,
      flip_vertical_ ? 1.0f : 0.0f};
  context_->UpdateSubresource(transform_buffer_.Get(), 0, nullptr, transform, 0,
                              0);
}
HRESULT Renderer::EnsureFrameTexture(UINT width, UINT height) {
  if (frame_texture_ && width == texture_width_ && height == texture_height_) return S_OK;
  frame_view_.Reset(); frame_texture_.Reset();
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &frame_texture_);
  if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(frame_texture_.Get(), nullptr, &frame_view_);
  if (SUCCEEDED(hr)) { texture_width_ = width; texture_height_ = height; }
  return hr;
}
HRESULT Renderer::CreateTarget() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
  HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&buffer));
  return SUCCEEDED(hr) ? device_->CreateRenderTargetView(buffer.Get(), nullptr, &target_) : hr;
}
HRESULT Renderer::CreateOverlayResources() {
  d2d_target_.Reset();
  overlay_text_brush_.Reset();
  overlay_background_brush_.Reset();
  if (!swap_chain_ || !d2d_factory_) return S_OK;
  Microsoft::WRL::ComPtr<IDXGISurface> surface;
  HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&surface));
  const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (SUCCEEDED(hr)) {
    hr = d2d_factory_->CreateDxgiSurfaceRenderTarget(surface.Get(), &properties,
                                                      &d2d_target_);
  }
  if (SUCCEEDED(hr)) hr = d2d_target_->CreateSolidColorBrush(
      D2D1::ColorF(D2D1::ColorF::Black, 0.68f), &overlay_background_brush_);
  if (SUCCEEDED(hr)) hr = d2d_target_->CreateSolidColorBrush(
      D2D1::ColorF(D2D1::ColorF::White), &overlay_text_brush_);
  return hr;
}
void Renderer::DrawOverlay() {
  if (!overlay_enabled_ || overlay_text_.empty() || !d2d_target_) return;
  const D2D1_SIZE_F size = d2d_target_->GetSize();
  const float width = std::min(size.width - 24.0f, 760.0f);
  const D2D1_RECT_F background = D2D1::RectF(12.0f, 12.0f, 12.0f + width, 84.0f);
  const D2D1_RECT_F text = D2D1::RectF(22.0f, 17.0f, 6.0f + width, 82.0f);
  d2d_target_->BeginDraw();
  d2d_target_->FillRoundedRectangle(D2D1::RoundedRect(background, 4.0f, 4.0f),
                                    overlay_background_brush_.Get());
  d2d_target_->DrawTextW(overlay_text_.c_str(),
                         static_cast<UINT32>(overlay_text_.size()),
                         overlay_format_.Get(), text, overlay_text_brush_.Get());
  const HRESULT hr = d2d_target_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) CreateOverlayResources();
}
HRESULT Renderer::Resize(UINT width, UINT height) {
  if (!swap_chain_ || !width || !height) return S_OK;
  d2d_target_.Reset(); overlay_text_brush_.Reset(); overlay_background_brush_.Reset();
  video_output_view_.Reset();
  context_->OMSetRenderTargets(0, nullptr, nullptr); target_.Reset();
  HRESULT hr = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (SUCCEEDED(hr)) hr = CreateTarget();
  if (SUCCEEDED(hr) && video_processor_) hr = CreateVideoOutputView();
  if (SUCCEEDED(hr)) hr = CreateOverlayResources();
  return hr;
}
void Renderer::Render() {
  if (!target_) return;
  RECT rect{}; GetClientRect(window_, &rect);
  const float width = static_cast<float>(rect.right), height = static_cast<float>(rect.bottom);
  const float source_aspect = static_cast<float>(source_width_) / source_height_;
  float view_width = width, view_height = width / source_aspect;
  if (view_height > height) { view_height = height; view_width = height * source_aspect; }
  D3D11_VIEWPORT viewport{(width-view_width)/2, (height-view_height)/2, view_width, view_height, 0, 1};
  const float black[] = {0, 0, 0, 1};
  context_->ClearRenderTargetView(target_.Get(), black);
  context_->RSSetViewports(1, &viewport);
  // Milestone 2 renders the newest captured texture inside this aspect-fit viewport.
  // Rendering currently runs from the UI timer. Never let a driver-side
  // vertical-blank wait block the Win32 message pump; a dedicated render
  // thread and configurable VSync will replace this in the video milestone.
  const HRESULT present_hr = swap_chain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
  if (present_hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
}
bool Renderer::PrepareNativeYuv(VideoPixelFormat format, UINT width,
                                UINT height) {
  return SUCCEEDED(EnsureYuvResources(format, width, height));
}

HRESULT Renderer::CreateVideoOutputView() {
  if (!video_device_ || !video_enumerator_ || !swap_chain_) return E_UNEXPECTED;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
  HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&buffer));
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC desc{};
  desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  desc.Texture2D.MipSlice = 0;
  if (SUCCEEDED(hr)) {
    hr = video_device_->CreateVideoProcessorOutputView(
        buffer.Get(), video_enumerator_.Get(), &desc, &video_output_view_);
  }
  return hr;
}

HRESULT Renderer::EnsureYuvResources(VideoPixelFormat format, UINT width,
                                     UINT height) {
  if (!video_device_ || !video_context_) return E_NOINTERFACE;
  if (format == VideoPixelFormat::Bgra32) return E_INVALIDARG;
  if (video_processor_ && yuv_texture_ && format == yuv_format_ &&
      width == yuv_width_ && height == yuv_height_) {
    return video_output_view_ ? S_OK : CreateVideoOutputView();
  }
  video_input_view_.Reset();
  yuv_texture_.Reset();
  yuv_staging_texture_.Reset();
  video_output_view_.Reset();
  video_processor_.Reset();
  video_enumerator_.Reset();

  D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
  content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  content.InputFrameRate = {60, 1};
  content.InputWidth = width;
  content.InputHeight = height;
  content.OutputFrameRate = {60, 1};
  content.OutputWidth = width;
  content.OutputHeight = height;
  content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  HRESULT hr = video_device_->CreateVideoProcessorEnumerator(
      &content, &video_enumerator_);
  if (SUCCEEDED(hr)) {
    hr = video_device_->CreateVideoProcessor(video_enumerator_.Get(), 0,
                                              &video_processor_);
  }

  D3D11_TEXTURE2D_DESC texture{};
  texture.Width = width;
  texture.Height = height;
  texture.MipLevels = 1;
  texture.ArraySize = 1;
  texture.Format = format == VideoPixelFormat::Nv12 ? DXGI_FORMAT_NV12
                                                     : DXGI_FORMAT_YUY2;
  texture.SampleDesc.Count = 1;
  texture.Usage = D3D11_USAGE_DEFAULT;
  texture.BindFlags = D3D11_BIND_DECODER;
  if (SUCCEEDED(hr)) hr = device_->CreateTexture2D(&texture, nullptr, &yuv_texture_);

  texture.Usage = D3D11_USAGE_STAGING;
  texture.BindFlags = 0;
  texture.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (SUCCEEDED(hr)) {
    hr = device_->CreateTexture2D(&texture, nullptr, &yuv_staging_texture_);
  }
  if (SUCCEEDED(hr)) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(yuv_staging_texture_.Get(), 0, D3D11_MAP_WRITE, 0,
                       &mapped);
    if (SUCCEEDED(hr)) context_->Unmap(yuv_staging_texture_.Get(), 0);
  }

  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input{};
  input.FourCC = 0;
  input.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  input.Texture2D.MipSlice = 0;
  input.Texture2D.ArraySlice = 0;
  if (SUCCEEDED(hr)) {
    hr = video_device_->CreateVideoProcessorInputView(
        yuv_texture_.Get(), video_enumerator_.Get(), &input,
        &video_input_view_);
  }
  if (SUCCEEDED(hr)) hr = CreateVideoOutputView();
  if (SUCCEEDED(hr)) {
    yuv_format_ = format;
    yuv_width_ = width;
    yuv_height_ = height;
  }
  return hr;
}

HRESULT Renderer::RenderYuv(VideoPixelFormat format,
                            const std::uint8_t* pixels, UINT width,
                            UINT height, UINT stride) {
  HRESULT hr = EnsureYuvResources(format, width, height);
  if (FAILED(hr)) return hr;
  source_width_ = width;
  source_height_ = height;
  D3D11_MAPPED_SUBRESOURCE mapped{};
  hr = context_->Map(yuv_staging_texture_.Get(), 0, D3D11_MAP_WRITE, 0,
                     &mapped);
  if (FAILED(hr)) return hr;
  const UINT row_bytes =
      width * (format == VideoPixelFormat::Yuy2 ? 2u : 1u);
  if (!mapped.pData || mapped.RowPitch < row_bytes || stride < row_bytes) {
    context_->Unmap(yuv_staging_texture_.Get(), 0);
    return E_INVALIDARG;
  }
  auto* mapped_destination = static_cast<std::uint8_t*>(mapped.pData);
  for (UINT row = 0; row < height; ++row) {
    auto* destination = mapped_destination +
                        static_cast<size_t>(row) * mapped.RowPitch;
    const UINT source_row = flip_vertical_ ? height - 1 - row : row;
    const auto* source = pixels + static_cast<size_t>(source_row) * stride;
    if (!flip_horizontal_) {
      std::memcpy(destination, source, row_bytes);
    } else if (format == VideoPixelFormat::Nv12) {
      for (UINT column = 0; column < width; ++column) {
        destination[column] = source[width - 1 - column];
      }
    } else {
      // YUY2 stores two pixels as Y0 U Y1 V. Reverse the macropixels and
      // exchange their two luma samples while retaining each shared U/V pair.
      for (UINT column = 0; column < width; column += 2) {
        const UINT source_offset = (width - 2 - column) * 2;
        const UINT destination_offset = column * 2;
        destination[destination_offset] = source[source_offset + 2];
        destination[destination_offset + 1] = source[source_offset + 1];
        destination[destination_offset + 2] = source[source_offset];
        destination[destination_offset + 3] = source[source_offset + 3];
      }
    }
  }
  if (format == VideoPixelFormat::Nv12) {
    const auto* source_uv = pixels + static_cast<size_t>(stride) * height;
    auto* destination_uv =
        mapped_destination + static_cast<size_t>(mapped.RowPitch) * height;
    for (UINT row = 0; row < height / 2; ++row) {
      auto* destination = destination_uv +
                          static_cast<size_t>(row) * mapped.RowPitch;
      const UINT source_row =
          flip_vertical_ ? height / 2 - 1 - row : row;
      const auto* source = source_uv + static_cast<size_t>(source_row) * stride;
      if (!flip_horizontal_) {
        std::memcpy(destination, source, width);
      } else {
        // NV12 chroma is interleaved UV; reverse pairs without swapping U/V.
        for (UINT column = 0; column < width; column += 2) {
          const UINT source_offset = width - 2 - column;
          destination[column] = source[source_offset];
          destination[column + 1] = source[source_offset + 1];
        }
      }
    }
  }
  context_->Unmap(yuv_staging_texture_.Get(), 0);
  context_->CopyResource(yuv_texture_.Get(), yuv_staging_texture_.Get());

  RECT client{};
  GetClientRect(window_, &client);
  const float client_width = static_cast<float>(client.right);
  const float client_height = static_cast<float>(client.bottom);
  const float aspect = static_cast<float>(width) / height;
  float view_width = client_width;
  float view_height = client_width / aspect;
  if (view_height > client_height) {
    view_height = client_height;
    view_width = client_height * aspect;
  }
  const RECT source{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  const RECT destination_rect{
      static_cast<LONG>((client_width - view_width) / 2),
      static_cast<LONG>((client_height - view_height) / 2),
      static_cast<LONG>((client_width + view_width) / 2),
      static_cast<LONG>((client_height + view_height) / 2)};
  const float black[] = {0, 0, 0, 1};
  context_->ClearRenderTargetView(target_.Get(), black);
  video_context_->VideoProcessorSetStreamSourceRect(video_processor_.Get(), 0,
                                                     TRUE, &source);
  video_context_->VideoProcessorSetStreamDestRect(video_processor_.Get(), 0,
                                                   TRUE, &destination_rect);
  video_context_->VideoProcessorSetOutputTargetRect(video_processor_.Get(),
                                                     TRUE, &destination_rect);
  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable = TRUE;
  stream.pInputSurface = video_input_view_.Get();
  hr = video_context_->VideoProcessorBlt(video_processor_.Get(),
                                         video_output_view_.Get(), 0, 1,
                                         &stream);
  if (FAILED(hr)) return hr;
  if (overlay_enabled_) {
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->Flush();
    DrawOverlay();
  }
  hr = swap_chain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
  return hr == DXGI_ERROR_WAS_STILL_DRAWING ? S_FALSE : hr;
}

HRESULT Renderer::RenderFrame(VideoPixelFormat format,
                              const std::uint8_t* pixels, UINT width,
                              UINT height, UINT stride) {
  if (!pixels || !width || !height || !target_) return E_INVALIDARG;
  if (format == VideoPixelFormat::Nv12 ||
      format == VideoPixelFormat::Yuy2) {
    return RenderYuv(format, pixels, width, height, stride);
  }
  HRESULT hr = EnsureFrameTexture(width, height);
  if (FAILED(hr)) return hr;
  source_width_ = width; source_height_ = height;
  context_->UpdateSubresource(frame_texture_.Get(), 0, nullptr, pixels, stride, 0);
  RECT rect{}; GetClientRect(window_, &rect);
  const float client_width = static_cast<float>(rect.right);
  const float client_height = static_cast<float>(rect.bottom);
  const float aspect = static_cast<float>(width) / height;
  float view_width = client_width, view_height = client_width / aspect;
  if (view_height > client_height) { view_height = client_height; view_width = client_height * aspect; }
  D3D11_VIEWPORT viewport{(client_width-view_width)/2, (client_height-view_height)/2,
                          view_width, view_height, 0, 1};
  const float black[] = {0, 0, 0, 1};
  context_->ClearRenderTargetView(target_.Get(), black);
  context_->OMSetRenderTargets(1, target_.GetAddressOf(), nullptr);
  context_->RSSetViewports(1, &viewport);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
  context_->VSSetConstantBuffers(0, 1, transform_buffer_.GetAddressOf());
  context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
  context_->PSSetShaderResources(0, 1, frame_view_.GetAddressOf());
  context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
  context_->Draw(3, 0);
  ID3D11ShaderResourceView* empty = nullptr;
  context_->PSSetShaderResources(0, 1, &empty);
  if (overlay_enabled_) {
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->Flush();
    DrawOverlay();
  }
  hr = swap_chain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
  return hr == DXGI_ERROR_WAS_STILL_DRAWING ? S_FALSE : hr;
}
}
