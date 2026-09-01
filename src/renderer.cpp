#include "renderer.h"
#include <algorithm>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
namespace cv {
namespace {
constexpr char kVertexShader[] = R"(
struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
  float2 positions[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
  float2 uvs[3] = { float2(0, 1), float2(0, -1), float2(2, 1) };
  Output output;
  output.position = float4(positions[id], 0, 1);
  output.uv = uvs[id];
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
  return hr;
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
  const D2D1_RECT_F background = D2D1::RectF(12.0f, 12.0f, 12.0f + width, 64.0f);
  const D2D1_RECT_F text = D2D1::RectF(22.0f, 17.0f, 6.0f + width, 62.0f);
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
  context_->OMSetRenderTargets(0, nullptr, nullptr); target_.Reset();
  HRESULT hr = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (SUCCEEDED(hr)) hr = CreateTarget();
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
HRESULT Renderer::RenderFrame(const std::uint8_t* pixels, UINT width, UINT height, UINT stride) {
  if (!pixels || !width || !height || !target_) return E_INVALIDARG;
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
