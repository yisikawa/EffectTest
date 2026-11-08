#pragma once
//======================================================================
//  Gfx.h : DirectX11 最小描画層（ビルボード専用）
//
//  ModelTest の Dx.cpp からは「デバイス生成・スワップチェーン・RTV/DSV」
//  の作法のみを踏襲し、影/トゥーン等は持たない。
//  頂点バッファは使わず SV_VertexID で 6 頂点を生成する。
//======================================================================
#include <d3d11.h>
#include <DirectXMath.h>

bool GfxInit(HWND hwnd, int width, int height);
void GfxShutdown();
bool GfxResize(int width, int height);

void GfxSetCamera(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);
void GfxBeginFrame(const float rgba[4]);
void GfxEndFrame();

// center / axisRight / axisUp はワールド空間ベクトル（axis は半幅・半高を含む）
void GfxDrawQuad(const float center[3],
                 const float axisRight[3],
                 const float axisUp[3],
                 const float rgba[4],
                 ID3D11ShaderResourceView* srv);

ID3D11Device*        GfxDevice();
ID3D11DeviceContext* GfxContext();

// 現在のバックバッファを PNG で保存（検証用）
bool GfxSaveBackBufferPNG(const wchar_t* path);
