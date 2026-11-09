#include "Tim.h"
#include <cstring>
#include <vector>

namespace
{
    inline uint32_t RdU32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

    // MSVC の複数文字リテラルと同じ並び（'DXT3' = 0x44585433）
    //  ※ ファイル上の 4 バイトは "DXT3" だが、dword として読むと 0x44585433 になる
    const uint32_t CC_DXT1 = 'DXT1';
    const uint32_t CC_DXT3 = 'DXT3';
    const uint32_t CC_DXT5 = 'DXT5';

    bool MakeSrv(ID3D11Device* dev, DXGI_FORMAT fmt, uint32_t w, uint32_t h, const void* data, UINT pitch, TimTexture& out)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = w;
        desc.Height           = h;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = fmt;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem     = data;
        init.SysMemPitch = pitch;

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(dev->CreateTexture2D(&desc, &init, &tex)) || tex == nullptr) return false;

        const HRESULT hr = dev->CreateShaderResourceView(tex, nullptr, &out.srv);
        tex->Release();
        return SUCCEEDED(hr) && out.srv != nullptr;
    }
}

//======================================================================
bool CreateTimTexture(ID3D11Device* device, const uint8_t* chunkBase, uint32_t chunkSize, TimTexture& out)
{
    if (device == nullptr || chunkBase == nullptr || chunkSize < 33 + 0x28 + 16) return false;

    // テクスチャ名（+17 から 16 バイト）
    char name[17] = {};
    memcpy(name, chunkBase + 17, 16);
    out.name = name;

    const uint8_t* hdr = chunkBase + 33;
    const uint32_t w = RdU32(hdr + 0x04);
    const uint32_t h = RdU32(hdr + 0x08);
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;

    out.width  = w;
    out.height = h;

    const uint32_t fourCC = RdU32(hdr + 0x28);
    const uint32_t off    = (uint32_t)(hdr - chunkBase) + 0x28;   // チャンク内オフセット

    if (fourCC == CC_DXT1)
    {
        const uint32_t dataOff = off + 12;
        const UINT pitch = ((w + 3) / 4) * 8;
        if (dataOff + pitch * ((h + 3) / 4) > chunkSize) return false;
        return MakeSrv(device, DXGI_FORMAT_BC1_UNORM, w, h, chunkBase + dataOff, pitch, out);
    }
    if (fourCC == CC_DXT3)
    {
        const uint32_t dataOff = off + 12;
        const UINT pitch = ((w + 3) / 4) * 16;
        if (dataOff + pitch * ((h + 3) / 4) > chunkSize) return false;
        return MakeSrv(device, DXGI_FORMAT_BC2_UNORM, w, h, chunkBase + dataOff, pitch, out);
    }
    if (fourCC == CC_DXT5)
    {
        const uint32_t dataOff = off + 12;
        const UINT pitch = ((w + 3) / 4) * 16;
        if (dataOff + pitch * ((h + 3) / 4) > chunkSize) return false;
        return MakeSrv(device, DXGI_FORMAT_BC3_UNORM, w, h, chunkBase + dataOff, pitch, out);
    }

    //--------------------------------------------------------------
    // 8bit パレット（256 色 BGRA + インデックス）
    //--------------------------------------------------------------
    const uint32_t palOff  = off;
    const uint32_t idxOff  = off + 0x400;
    if (idxOff + (uint64_t)w * h > chunkSize) return false;

    const uint32_t* pal = reinterpret_cast<const uint32_t*>(chunkBase + palOff);
    const uint8_t*  idx = chunkBase + idxOff;

    std::vector<uint32_t> buf((size_t)w * h);
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(idx + (size_t)y * w);   // 1 バイト/画素
        uint32_t* dst = buf.data() + (size_t)(h - 1 - y) * w;                            // 縦反転
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint8_t i = reinterpret_cast<const uint8_t*>(src)[x];
            dst[x] = pal[i];
        }
    }

    //--------------------------------------------------------------
    // 不透明領域（アルファ > 0）のバウンディングボックスを UV で持つ
    //   0x21 を持たないジェネレータの UV 決定に使う（--uvcrop）
    //--------------------------------------------------------------
    {
        uint32_t x0 = w, y0 = h, x1 = 0, y1 = 0;
        bool any = false;
        for (uint32_t y = 0; y < h; ++y)
        {
            const uint32_t* row = buf.data() + (size_t)y * w;
            for (uint32_t x = 0; x < w; ++x)
            {
                if ((row[x] >> 24) == 0) continue;      // A=0 は透明
                any = true;
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
        }
        if (any)
        {
            out.uvb[0] = (float)x0 / (float)w;
            out.uvb[1] = (float)y0 / (float)h;
            out.uvb[2] = (float)(x1 + 1) / (float)w;
            out.uvb[3] = (float)(y1 + 1) / (float)h;
        }

        // 黒（輝度 < 8）の割合: 黒背景のテクスチャは加算合成が前提（黒 = 透明）
        uint32_t black = 0;
        for (size_t i = 0; i < buf.size(); ++i)
        {
            const uint32_t p = buf[i];
            const uint32_t r = p & 0xFF, g = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF;
            if (r + g + b < 96) ++black;   // 暗部（黒〜濃灰）
        }
        out.blackRatio = (float)black / (float)buf.size();
    }

    return MakeSrv(device, DXGI_FORMAT_B8G8R8A8_UNORM, w, h, buf.data(), w * 4, out);
}

//======================================================================
bool CreateWhiteTexture(ID3D11Device* device, TimTexture& out)
{
    if (device == nullptr) return false;
    const uint32_t px = 0xFFFFFFFFu;
    out.name   = "white";
    out.width  = 1;
    out.height = 1;
    return MakeSrv(device, DXGI_FORMAT_B8G8R8A8_UNORM, 1, 1, &px, 4, out);
}
