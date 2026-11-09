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
// uvRect = (u0,v0,u1,v1)。nullptr ならテクスチャ全面。
void GfxDrawQuad(const float center[3],
                 const float axisRight[3],
                 const float axisUp[3],
                 const float rgba[4],
                 ID3D11ShaderResourceView* srv);

void GfxDrawQuad(const float center[3],
                 const float axisRight[3],
                 const float axisUp[3],
                 const float rgba[4],
                 const float uvRect[4],
                 ID3D11ShaderResourceView* srv);

//------------------------------------------------------------------
//  合成モード（AltanaView の SetRenderState 実測値に対応）
//    ALPHA_ADD : SRC=SRCALPHA / DST=ONE         … 既定（エフェクト用）
//    ADD       : SRC=ONE      / DST=ONE         … 純加算（旧来）
//    ALPHA     : SRC=SRCALPHA / DST=INVSRCALPHA … 通常の透過（モデル用）
//------------------------------------------------------------------
enum GfxBlendMode
{
    GFX_BLEND_ALPHA_ADD = 0,
    GFX_BLEND_ADD       = 1,
    GFX_BLEND_ALPHA     = 2,
    GFX_BLEND_COUNT     = 3,
};

void         GfxSetBlendMode(GfxBlendMode mode);
GfxBlendMode GfxGetBlendMode();
const char*  GfxBlendModeName(GfxBlendMode mode);

// テクスチャの色順（FFXI の TIM は BGRA 格納のため、既定で B/R を入れ替える）
void GfxSetColorSwap(bool bgr);
bool GfxGetColorSwap();

ID3D11Device*        GfxDevice();
ID3D11DeviceContext* GfxContext();

// 現在のバックバッファを PNG で保存（検証用）
bool GfxSaveBackBufferPNG(const wchar_t* path);
