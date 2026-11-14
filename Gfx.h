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
    GFX_BLEND_NONE      = 3,   // ブレンド無効（アルファクリップ専用）
    GFX_BLEND_COUNT     = 4,
};

void         GfxSetBlendMode(GfxBlendMode mode);
GfxBlendMode GfxGetBlendMode();
const char*  GfxBlendModeName(GfxBlendMode mode);

// 深度テストの切替（--ffxivm on で ffxitool5 と同じ「深度テスト無し」にする）
//   既定 on（DepthEnable=TRUE / 書き込み無し）。off で DepthEnable=FALSE の
//   第 2 の DSS に切り替える。
void GfxSetDepthTest(bool on);
bool GfxDepthTest();

//------------------------------------------------------------------
//  アルファクリップ（D3D8 の ALPHATESTENABLE/ALPHAREF/ALPHAFUNC 相当）
//    D3D11 には固定機能のアルファテストが無いため、PS で
//      if (texA * colA < ref) discard;
//    を行う。ref は 0..255（既定 128 = 0.5）。
//    ※ ffxitool5 はアルファテストを使っておらず、この機能は当方の追加モード。
//------------------------------------------------------------------
void GfxSetAlphaClip(bool on, int ref255);
bool GfxAlphaClip();
int  GfxAlphaRef();

// テクスチャの色順（FFXI の TIM は BGRA 格納のため、既定で B/R を入れ替える）
void GfxSetColorSwap(bool bgr);
bool GfxGetColorSwap();

//------------------------------------------------------------------
//  色のゲイン（テクスチャステージ相当）
//    ffxitool5 は COLOROP/ALPHAOP = MODULATE4X（RGB も α も ×4）で、
//    その後 SRCALPHA/ONE で加算合成する。当方の従来式は RGB×1.5 / α×1。
//    rgb   : RGB の倍率（既定 1.5）
//    alpha : α の倍率（既定 1.0。ffxi 相当は 4.0）
//------------------------------------------------------------------
void GfxSetColorGain(float rgb, float alpha);
float GfxColorGainRgb();
float GfxColorGainAlpha();
ID3D11Device*        GfxDevice();
ID3D11DeviceContext* GfxContext();

// 現在のバックバッファを PNG で保存（検証用）
bool GfxSaveBackBufferPNG(const wchar_t* path);
