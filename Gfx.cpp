#include "Gfx.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <cstdio>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using namespace DirectX;

//----------------------------------------------------------------------
// 定数バッファ（フレーム行列 + インスタンス）
//----------------------------------------------------------------------
struct CBData
{
    XMFLOAT4X4 viewProj;
    XMFLOAT4   center;
    XMFLOAT4   axisR;
    XMFLOAT4   axisU;
    XMFLOAT4   color;
};

static const char* kShader =
"cbuffer CB : register(b0)\n"
"{\n"
"    float4x4 gViewProj;\n"
"    float4   gCenter;\n"
"    float4   gAxisR;\n"
"    float4   gAxisU;\n"
"    float4   gColor;\n"
"};\n"
"Texture2D    gTex : register(t0);\n"
"SamplerState gSmp : register(s0);\n"
"struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
"VSOut VSMain(uint id : SV_VertexID)\n"
"{\n"
"    float2 c[6] = { float2(-1,-1), float2(-1,1), float2(1,-1),\n"
"                    float2(1,-1),  float2(-1,1), float2(1,1) };\n"
"    float2 t  = c[id];\n"
"    float3 wp = gCenter.xyz + gAxisR.xyz * t.x + gAxisU.xyz * t.y;\n"
"    VSOut o;\n"
"    o.pos = mul(float4(wp, 1.0), gViewProj);\n"
"    o.uv  = float2((t.x + 1.0) * 0.5, 1.0 - (t.y + 1.0) * 0.5);\n"
"    o.col = gColor;\n"
"    return o;\n"
"}\n"
"float4 PSMain(VSOut i) : SV_TARGET\n"
"{\n"
"    float4 t = gTex.Sample(gSmp, i.uv);\n"
"    // FFXI のエフェクト用テクスチャは RGB=0 で alpha に形状を持つものが多い。\n"
"    // そのため「RGB と alpha の大きい方」を輝度として扱う。\n"
"    float3 s = max(t.rgb, float3(t.a, t.a, t.a));\n"
"    float  m = saturate(max(s.r, max(s.g, s.b)));   // 形状（ブレンド用 alpha）\n"
"    return float4(s * i.col.rgb * i.col.a * 1.5, m * i.col.a);\n"
"}\n";

//----------------------------------------------------------------------
// 内部状態
//----------------------------------------------------------------------
namespace
{
    ID3D11Device*            g_Device  = nullptr;
    ID3D11DeviceContext*     g_Context = nullptr;
    IDXGISwapChain*          g_Swap    = nullptr;
    ID3D11RenderTargetView*  g_RTV     = nullptr;
    ID3D11DepthStencilView*  g_DSV     = nullptr;
    ID3D11DepthStencilState* g_DSS     = nullptr;
    ID3D11BlendState*        g_Blends[GFX_BLEND_COUNT] = {};
    GfxBlendMode             g_BlendMode = GFX_BLEND_ALPHA_ADD;
    ID3D11RasterizerState*   g_Raster  = nullptr;
    ID3D11SamplerState*      g_Sampler = nullptr;
    ID3D11VertexShader*      g_VS      = nullptr;
    ID3D11PixelShader*       g_PS      = nullptr;
    ID3D11Buffer*            g_CB      = nullptr;
    ID3D11ShaderResourceView* g_White  = nullptr;   // 1x1 白（テクスチャ無し時の代替）

    int       g_Width  = 0;
    int       g_Height = 0;
    XMMATRIX  g_ViewProj = XMMatrixIdentity();

    bool CompileShader(const char* entry, const char* target, ID3DBlob** ppBlob)
    {
        ID3DBlob* err = nullptr;
        const HRESULT hr = D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr,
                                      entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, ppBlob, &err);
        if (FAILED(hr))
        {
            if (err) { OutputDebugStringA((const char*)err->GetBufferPointer()); err->Release(); }
            return false;
        }
        if (err) err->Release();
        return true;
    }
}

ID3D11Device*        GfxDevice()  { return g_Device; }
ID3D11DeviceContext* GfxContext() { return g_Context; }

//======================================================================
bool GfxInit(HWND hwnd, int width, int height)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount        = 1;
    sd.BufferDesc.Width   = (UINT)width;
    sd.BufferDesc.Height  = (UINT)height;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = hwnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                             levels, _countof(levels), D3D11_SDK_VERSION,
                                             &sd, &g_Swap, &g_Device, &got, &g_Context)))
        return false;

    if (!GfxResize(width, height)) return false;

    //--------------------------------------------------------------
    // シェーダ
    //--------------------------------------------------------------
    ID3DBlob* vsb = nullptr;
    ID3DBlob* psb = nullptr;
    if (!CompileShader("VSMain", "vs_4_0", &vsb)) return false;
    if (!CompileShader("PSMain", "ps_4_0", &psb)) { vsb->Release(); return false; }

    const HRESULT hrv = g_Device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_VS);
    vsb->Release();
    const HRESULT hrp = g_Device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_PS);
    psb->Release();
    if (FAILED(hrv) || FAILED(hrp)) return false;

    //--------------------------------------------------------------
    // 定数バッファ
    //--------------------------------------------------------------
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = sizeof(CBData);
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_Device->CreateBuffer(&bd, nullptr, &g_CB))) return false;

    //--------------------------------------------------------------
    // ステート（合成モード別 / Z 書き込みなし）
    //--------------------------------------------------------------
    struct BlendPreset { D3D11_BLEND src; D3D11_BLEND dst; };
    static const BlendPreset kBlendPresets[GFX_BLEND_COUNT] =
    {
        { D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE           },   // GFX_BLEND_ALPHA_ADD
        { D3D11_BLEND_ONE,       D3D11_BLEND_ONE           },   // GFX_BLEND_ADD
        { D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA },   // GFX_BLEND_ALPHA
    };

    for (int i = 0; i < GFX_BLEND_COUNT; ++i)
    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable           = TRUE;
        blendDesc.RenderTarget[0].SrcBlend              = kBlendPresets[i].src;
        blendDesc.RenderTarget[0].DestBlend             = kBlendPresets[i].dst;
        blendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(g_Device->CreateBlendState(&blendDesc, &g_Blends[i]))) return false;
    }

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(g_Device->CreateDepthStencilState(&dsDesc, &g_DSS))) return false;

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode        = D3D11_FILL_SOLID;
    rsDesc.CullMode        = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable = TRUE;
    if (FAILED(g_Device->CreateRasterizerState(&rsDesc, &g_Raster))) return false;

    D3D11_SAMPLER_DESC smDesc = {};
    smDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    smDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    smDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smDesc.MaxLOD   = D3D11_FLOAT32_MAX;
    if (FAILED(g_Device->CreateSamplerState(&smDesc, &g_Sampler))) return false;

    //--------------------------------------------------------------
    // 1x1 白テクスチャ（DAT 未読込でも軸や板を描けるように）
    //--------------------------------------------------------------
    {
        const uint32_t px = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = 1;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd2 = {};
        sd2.pSysMem = &px;
        sd2.SysMemPitch = 4;

        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_Device->CreateTexture2D(&td, &sd2, &tex)) && tex != nullptr)
        {
            g_Device->CreateShaderResourceView(tex, nullptr, &g_White);
            tex->Release();
        }
    }

    return true;
}

//======================================================================
bool GfxResize(int width, int height)
{
    if (g_Device == nullptr || g_Swap == nullptr) return false;
    if (width <= 0 || height <= 0) return false;

    g_Width  = width;
    g_Height = height;

    if (g_RTV) { g_RTV->Release(); g_RTV = nullptr; }
    if (g_DSV) { g_DSV->Release(); g_DSV = nullptr; }

    g_Context->OMSetRenderTargets(0, nullptr, nullptr);

    if (FAILED(g_Swap->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0)))
        return false;

    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_Swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) || back == nullptr)
        return false;
    const HRESULT hrRtv = g_Device->CreateRenderTargetView(back, nullptr, &g_RTV);
    back->Release();
    if (FAILED(hrRtv)) return false;

    // 深度バッファ
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width            = (UINT)width;
    dd.Height           = (UINT)height;
    dd.MipLevels        = 1;
    dd.ArraySize        = 1;
    dd.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage            = D3D11_USAGE_DEFAULT;
    dd.BindFlags        = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depth = nullptr;
    if (FAILED(g_Device->CreateTexture2D(&dd, nullptr, &depth)) || depth == nullptr) return false;
    const HRESULT hrDsv = g_Device->CreateDepthStencilView(depth, nullptr, &g_DSV);
    depth->Release();
    return SUCCEEDED(hrDsv);
}

//======================================================================
void GfxShutdown()
{
    if (g_Context) g_Context->ClearState();

    if (g_White)   { g_White->Release();   g_White = nullptr; }
    if (g_CB)      { g_CB->Release();      g_CB = nullptr; }
    if (g_PS)      { g_PS->Release();      g_PS = nullptr; }
    if (g_VS)      { g_VS->Release();      g_VS = nullptr; }
    if (g_Sampler) { g_Sampler->Release(); g_Sampler = nullptr; }
    if (g_Raster)  { g_Raster->Release();  g_Raster = nullptr; }
    for (int i = 0; i < GFX_BLEND_COUNT; ++i)
    {
        if (g_Blends[i]) { g_Blends[i]->Release(); g_Blends[i] = nullptr; }
    }
    if (g_DSS)     { g_DSS->Release();     g_DSS = nullptr; }
    if (g_DSV)     { g_DSV->Release();     g_DSV = nullptr; }
    if (g_RTV)     { g_RTV->Release();     g_RTV = nullptr; }
    if (g_Swap)    { g_Swap->Release();    g_Swap = nullptr; }
    if (g_Context) { g_Context->Release(); g_Context = nullptr; }
    if (g_Device)  { g_Device->Release();  g_Device = nullptr; }
}

//======================================================================
void GfxSetCamera(const XMMATRIX& view, const XMMATRIX& proj)
{
    // HLSL は cbuffer の行列を列優先として読むため、転置して渡す
    // （VS 側は mul(float4(world,1), gViewProj) で行ベクトル規約）
    g_ViewProj = XMMatrixMultiplyTranspose(view, proj);
}

//======================================================================
//  合成モード
//======================================================================
void GfxSetBlendMode(GfxBlendMode mode)
{
    if (mode < 0 || mode >= GFX_BLEND_COUNT) return;
    g_BlendMode = mode;
}

GfxBlendMode GfxGetBlendMode() { return g_BlendMode; }

const char* GfxBlendModeName(GfxBlendMode mode)
{
    switch (mode)
    {
    case GFX_BLEND_ALPHA_ADD: return "alpha_add";
    case GFX_BLEND_ADD:       return "add";
    case GFX_BLEND_ALPHA:     return "alpha";
    default:                  return "?";
    }
}

//======================================================================
void GfxBeginFrame(const float rgba[4])
{
    if (g_Context == nullptr) return;

    g_Context->OMSetRenderTargets(1, &g_RTV, g_DSV);

    D3D11_VIEWPORT vp = {};
    vp.Width    = (float)g_Width;
    vp.Height   = (float)g_Height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_Context->RSSetViewports(1, &vp);

    g_Context->ClearRenderTargetView(g_RTV, rgba);
    g_Context->ClearDepthStencilView(g_DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

    g_Context->IASetInputLayout(nullptr);
    g_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_Context->VSSetShader(g_VS, nullptr, 0);
    g_Context->PSSetShader(g_PS, nullptr, 0);
    g_Context->PSSetSamplers(0, 1, &g_Sampler);
    g_Context->OMSetBlendState(g_Blends[g_BlendMode], nullptr, 0xFFFFFFFF);
    g_Context->OMSetDepthStencilState(g_DSS, 0);
    g_Context->RSSetState(g_Raster);
}

//======================================================================
void GfxEndFrame()
{
    if (g_Swap) g_Swap->Present(1, 0);
}

//======================================================================
void GfxDrawQuad(const float center[3], const float axisRight[3], const float axisUp[3],
                 const float rgba[4], ID3D11ShaderResourceView* srv)
{
    if (g_Context == nullptr || g_CB == nullptr) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(g_Context->Map(g_CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

    CBData* cb = (CBData*)mapped.pData;
    XMStoreFloat4x4(&cb->viewProj, g_ViewProj);

    cb->center = XMFLOAT4(center[0], center[1], center[2], 1.0f);
    cb->axisR  = XMFLOAT4(axisRight[0], axisRight[1], axisRight[2], 0.0f);
    cb->axisU  = XMFLOAT4(axisUp[0], axisUp[1], axisUp[2], 0.0f);
    cb->color  = XMFLOAT4(rgba[0], rgba[1], rgba[2], rgba[3]);

    g_Context->Unmap(g_CB, 0);

    g_Context->VSSetConstantBuffers(0, 1, &g_CB);
    g_Context->PSSetConstantBuffers(0, 1, &g_CB);

    if (srv == nullptr) srv = g_White;                       // 代替: 1x1 白
    g_Context->PSSetShaderResources(0, 1, &srv);

    g_Context->Draw(6, 0);
}

namespace
{
    void ShotDiag(const char* msg, HRESULT hr)
    {
        FILE* fp = nullptr;
        if (fopen_s(&fp, "effecttest_shot_diag.txt", "ab") == 0 && fp)
        {
            fprintf(fp, "%-24s hr=0x%08X\n", msg, (unsigned)hr);
            fclose(fp);
        }
    }
}

//======================================================================
// バックバッファを PNG 保存（検証・スクリーンショット用）
//======================================================================
bool GfxSaveBackBufferPNG(const wchar_t* path)
{
    if (g_Swap == nullptr || g_Device == nullptr || g_Context == nullptr || path == nullptr) return false;

    ID3D11Texture2D* back = nullptr;
    HRESULT hrBack = g_Swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    ShotDiag("GetBuffer", hrBack);
    if (FAILED(hrBack) || back == nullptr) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    back->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage          = D3D11_USAGE_STAGING;
    sd.BindFlags      = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags      = 0;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;

    ID3D11Texture2D* stage = nullptr;
    HRESULT hr = g_Device->CreateTexture2D(&sd, nullptr, &stage);
    ShotDiag("CreateTexture2D(staging)", hr);
    if (SUCCEEDED(hr) && stage != nullptr) g_Context->CopyResource(stage, back);
    back->Release();
    if (FAILED(hr) || stage == nullptr) return false;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hrMap = g_Context->Map(stage, 0, D3D11_MAP_READ, 0, &mapped);
    ShotDiag("Map", hrMap);
    if (FAILED(hrMap))
    {
        stage->Release();
        return false;
    }

    bool ok = false;
    IWICImagingFactory* factory = nullptr;
    HRESULT hStep = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     __uuidof(IWICImagingFactory), (void**)&factory);
    ShotDiag("CoCreateInstance", hStep);
    if (SUCCEEDED(hStep) && factory)
    {
        IWICStream*            stream = nullptr;
        IWICBitmapEncoder*     enc    = nullptr;
        IWICBitmapFrameEncode* frame  = nullptr;
        IPropertyBag2*         props  = nullptr;

        hStep = factory->CreateStream(&stream);                 ShotDiag("CreateStream", hStep);
        if (SUCCEEDED(hStep)) { hStep = stream->InitializeFromFilename(path, GENERIC_WRITE); ShotDiag("InitializeFromFilename", hStep); }
        if (SUCCEEDED(hStep)) { hStep = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc); ShotDiag("CreateEncoder", hStep); }
        if (SUCCEEDED(hStep)) { hStep = enc->Initialize(stream, WICBitmapEncoderNoCache); ShotDiag("EncoderInitialize", hStep); }
        if (SUCCEEDED(hStep)) { hStep = enc->CreateNewFrame(&frame, &props); ShotDiag("CreateNewFrame", hStep); }
        if (SUCCEEDED(hStep)) { hStep = frame->Initialize(props); ShotDiag("FrameInitialize", hStep); }
        if (SUCCEEDED(hStep)) { hStep = frame->SetSize(desc.Width, desc.Height); ShotDiag("SetSize", hStep); }

        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
        if (SUCCEEDED(hStep)) { hStep = frame->SetPixelFormat(&fmt); ShotDiag("SetPixelFormat", hStep); }

        char fmtbuf[64] = {};
        sprintf_s(fmtbuf, "format=%08lX-%04hX-%04hX", fmt.Data1, fmt.Data2, fmt.Data3);
        ShotDiag(fmtbuf, S_OK);

        if (SUCCEEDED(hStep))
        {
            hStep = frame->WritePixels(desc.Height, mapped.RowPitch,
                                       mapped.RowPitch * desc.Height, (BYTE*)mapped.pData);
            ShotDiag("WritePixels", hStep);
        }
        if (SUCCEEDED(hStep)) { hStep = frame->Commit();  ShotDiag("FrameCommit", hStep); }
        if (SUCCEEDED(hStep)) { hStep = enc->Commit();    ShotDiag("EncoderCommit", hStep); }
        ok = SUCCEEDED(hStep);

        if (frame)   frame->Release();
        if (props)   props->Release();
        if (enc)     enc->Release();
        if (stream)  stream->Release();
        factory->Release();
    }

    g_Context->Unmap(stage, 0);
    stage->Release();
    return ok;
}


