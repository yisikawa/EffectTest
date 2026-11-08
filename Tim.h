#pragma once
//======================================================================
//  Tim.h : FFXI テクスチャチャンク（type 0x20）→ D3D11 SRV
//
//  ModelTest の CData::LoadTextureFromFile() の 0x20 分岐を移植。
//      +33+0x04 : width      +33+0x08 : height
//      +33+0x28 : fourCC（'DXT1'/'DXT3'/'DXT5'）または 256 色パレット
//      非圧縮時 : +33+0x28+0x400 から 8bit インデックス（縦反転して展開）
//======================================================================
#include <d3d11.h>
#include <cstdint>
#include <string>

struct TimTexture
{
    std::string             name;
    uint32_t                width = 0;
    uint32_t                height = 0;
    ID3D11ShaderResourceView* srv = nullptr;

    void Release()
    {
        if (srv) { srv->Release(); srv = nullptr; }
        name.clear();
        width = height = 0;
    }
};

// chunkBase はチャンク先頭（4 文字名から始まる位置）
bool CreateTimTexture(ID3D11Device* device, const uint8_t* chunkBase, uint32_t chunkSize, TimTexture& out);

// 1x1 白テクスチャ（テクスチャが無い DAT 用フォールバック）
bool CreateWhiteTexture(ID3D11Device* device, TimTexture& out);
