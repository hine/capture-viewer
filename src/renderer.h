#pragma once
#include <cstdint>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <utility>
#include <wrl/client.h>
namespace cv {
class Renderer {
 public:
  HRESULT Initialize(HWND window);
  HRESULT Resize(UINT width, UINT height);
  void Render();
  HRESULT RenderFrame(const std::uint8_t* pixels, UINT width, UINT height,
                      UINT stride);
  void SetSourceSize(UINT width, UINT height) { source_width_ = width; source_height_ = height; }
  void SetOverlay(bool enabled, std::wstring text) {
    overlay_enabled_ = enabled;
    overlay_text_ = std::move(text);
  }
 private:
  HRESULT CreateTarget();
  HRESULT CreateOverlayResources();
  void DrawOverlay();
  HRESULT CreateShaders();
  HRESULT EnsureFrameTexture(UINT width, UINT height);
  HWND window_ = nullptr;
  UINT source_width_ = 16, source_height_ = 9;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_texture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> frame_view_;
  Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
  Microsoft::WRL::ComPtr<ID2D1RenderTarget> d2d_target_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlay_text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlay_background_brush_;
  Microsoft::WRL::ComPtr<IDWriteFactory> write_factory_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> overlay_format_;
  std::wstring overlay_text_;
  bool overlay_enabled_ = false;
  UINT texture_width_ = 0, texture_height_ = 0;
};
}
