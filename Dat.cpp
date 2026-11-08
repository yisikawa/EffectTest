#include "Dat.h"

//======================================================================
//  読み込み
//======================================================================
bool DatFile::Load(const char* path)
{
    Unload();
    if (path == nullptr || path[0] == '\0') return false;

    FILE* fp = nullptr;
    if (fopen_s(&fp, path, "rb") != 0 || fp == nullptr) return false;

    fseek(fp, 0, SEEK_END);
    const long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) { fclose(fp); return false; }

    m_Data.resize((size_t)size);
    const size_t read = fread(m_Data.data(), 1, (size_t)size, fp);
    fclose(fp);
    if (read != (size_t)size) { Unload(); return false; }

    m_Path = path;

    //--------------------------------------------------------------
    // チャンク走査（ModelTest / AltanaView と同一規則）
    //--------------------------------------------------------------
    uint32_t pos = 0;
    while (InRange(pos, 16))
    {
        uint32_t hdr = 0;
        memcpy(&hdr, m_Data.data() + pos + 4, 4);

        uint32_t next = (hdr >> 3) & 0x7FFFF0u;
        uint32_t type = hdr & 0x7Fu;
        if (next < 16) break;                  // 終端
        if (pos + next > Size()) break;        // 壊れたサイズは捨てる

        DatChunk c = {};
        memcpy(c.name, m_Data.data() + pos, 4);
        c.name[4] = '\0';
        for (int i = 0; i < 4; ++i)            // 表示用に非印字を '.'
            if ((unsigned char)c.name[i] < 0x20 || (unsigned char)c.name[i] > 0x7E) c.name[i] = '.';
        c.type   = type;
        c.offset = pos;
        c.size   = next;

        m_Chunks.push_back(c);
        pos += next;
    }
    return !m_Chunks.empty();
}

void DatFile::Unload()
{
    m_Chunks.clear();
    m_Data.clear();
    m_Data.shrink_to_fit();
    m_Path.clear();
}

//======================================================================
//  検索
//======================================================================
const DatChunk* DatFile::Find(const char* name4, uint32_t type) const
{
    if (name4 == nullptr) return nullptr;
    for (const DatChunk& c : m_Chunks)
    {
        if (memcmp(c.name, name4, 4) != 0) continue;   // 4 文字のみ比較
        if (type != TYPE_ANY && c.type != type) continue;
        return &c;
    }
    return nullptr;
}

const DatChunk* DatFile::FindByType(uint32_t type) const
{
    for (const DatChunk& c : m_Chunks)
        if (c.type == type) return &c;
    return nullptr;
}
