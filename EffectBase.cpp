#include "EffectBase.h"
#include <windows.h>
#include <cstring>

//======================================================================
bool EffectBase::Load(ID3D11Device* device, const char* datPath)
{
    Unload();
    if (datPath == nullptr) return false;

    // 高分解能タイマ（GetTickCount64 は約 16ms 分解能で 0 になり得るため）
    LARGE_INTEGER freq = {}, t0 = {}, t1 = {};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    if (!m_Dat.Load(datPath)) return false;
    m_Path = datPath;

    for (const DatChunk& c : m_Dat.Chunks())
    {
        if (c.type == 0x05 && c.size > 0x90)
        {
            const uint8_t* p = m_Dat.At(c.offset);
            if (p == nullptr) continue;

            BaseGenerator g;
            g.chunk       = c;
            g.payloadOff  = c.offset + 0x90;
            g.payloadSize = c.size - 0x90;

            memcpy(&g.id,     p + 0x10, 4);
            memcpy(&g.frames, p + 0x76, 2);
            g.count = p[0x78];
            g.flag  = p[0x79];

            uint32_t o = 0;
            memcpy(&o, p + 0x80, 4); g.tblA = g.payloadOff + (o - 0x90);
            memcpy(&o, p + 0x84, 4); g.tblB = g.payloadOff + (o - 0x90);
            memcpy(&o, p + 0x88, 4); g.tblC = g.payloadOff + (o - 0x90);
            memcpy(&o, p + 0x8C, 4); g.tblD = g.payloadOff + (o - 0x90);

            m_Generators.push_back(g);
        }
        else if (c.type == 0x20)
        {
            if (device == nullptr) continue;
            TimTexture t;
            if (CreateTimTexture(device, m_Dat.At(c.offset), c.size, t))
                m_Textures.emplace_back(std::string(c.name), t);
        }
        else if (c.type == 0x07)
        {
            m_Timelines.push_back(c);
        }
    }

    m_LoadMs = 0;
    QueryPerformanceCounter(&t1);
    if (freq.QuadPart > 0)
        m_LoadMs = (int)((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);
    return true;
}

//======================================================================
void EffectBase::Unload()
{
    for (auto& kv : m_Textures) kv.second.Release();
    m_Textures.clear();
    m_Generators.clear();
    m_Timelines.clear();
    m_Dat.Unload();
    m_Path.clear();
    m_LoadMs = 0;
}

//======================================================================
const BaseGenerator* EffectBase::FindGenerator(const char* name4) const
{
    if (name4 == nullptr) return nullptr;
    for (const BaseGenerator& g : m_Generators)
        if (memcmp(g.chunk.name, name4, 4) == 0) return &g;
    return nullptr;
}

const DatChunk* EffectBase::FindTimeline(const char* name4) const
{
    if (name4 == nullptr) return nullptr;
    for (const DatChunk& c : m_Timelines)
        if (memcmp(c.name, name4, 4) == 0) return &c;
    return nullptr;
}

ID3D11ShaderResourceView* EffectBase::FindTexture(const char* name4) const
{
    if (name4 == nullptr) return nullptr;
    for (const auto& kv : m_Textures)
        if (kv.first.compare(0, 4, name4 ? name4 : "") == 0) return kv.second.srv;
    return nullptr;
}
