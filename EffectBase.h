#pragma once
//======================================================================
//  EffectBase.h : ベースエフェクト（ROM\0\0.DAT = syst）の索引
//
//  List/Effect/*.csv の先頭行 `0/0,syst` が示すとおり、この DAT は
//  全エフェクトが共有する土台（共通ジェネレータ / テクスチャ / サブエフェクト）。
//  起動時に必ず読み込み、個別エフェクトで名前が解決できない場合の
//  フォールバック先として使う。
//======================================================================
#include "Dat.h"
#include "Tim.h"
#include <d3d11.h>
#include <string>
#include <vector>

struct BaseGenerator
{
    DatChunk chunk;
    uint32_t payloadOff  = 0;
    uint32_t payloadSize = 0;
    uint32_t tblA = 0, tblB = 0, tblC = 0, tblD = 0;
    uint32_t id     = 0;
    uint16_t frames = 0;
    uint8_t  count  = 0;
    uint8_t  flag   = 0;
};

class EffectBase
{
public:
    bool Load(ID3D11Device* device, const char* datPath);
    void Unload();
    bool IsLoaded() const { return m_Dat.IsLoaded(); }

    const DatFile& Dat()  const { return m_Dat; }
    const char*    Path() const { return m_Path.c_str(); }
    int            LoadMs() const { return m_LoadMs; }

    size_t ChunkCount()    const { return m_Dat.Chunks().size(); }
    size_t GeneratorCount()const { return m_Generators.size(); }
    size_t TextureCount()  const { return m_Textures.size(); }
    size_t TimelineCount() const { return m_Timelines.size(); }

    const BaseGenerator*      FindGenerator(const char* name4) const;
    const DatChunk*           FindTimeline(const char* name4) const;
    ID3D11ShaderResourceView* FindTexture(const char* name4) const;

private:
    DatFile             m_Dat;
    std::string         m_Path;
    std::vector<BaseGenerator>            m_Generators;
    std::vector<std::pair<std::string, TimTexture>> m_Textures;
    std::vector<DatChunk>                 m_Timelines;
    int                 m_LoadMs = 0;
};
