#pragma once
//======================================================================
//  Dat.h  : FFXI DAT ファイル / チャンク走査
//
//  ModelTest の Model_Common.cpp と同じ走査規則を、effect 専用に
//  最小構成で再実装したもの。
//      next = (u32@pos+4 >> 3) & 0x7FFFF0
//      type =  u32@pos+4 & 0x7F
//      next < 16 で終端
//  チャンク先頭 4 バイトは 4 文字のブロック名（"main","g000","fai0" …）。
//======================================================================
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct DatChunk
{
    char     name[5];     // NUL 終端済み（"g000" 等）
    uint32_t type;        // 0x00 end / 0x01 / 0x05 generator / 0x07 main / 0x19 / 0x1F / 0x20 tex / 0x21 / 0x3D
    uint32_t offset;      // ファイル先頭からのバイト位置
    uint32_t size;        // 次チャンクまでのサイズ
    inline uint32_t End() const { return offset + size; }
};

class DatFile
{
public:
    ~DatFile() { Unload(); }

    // 読み込み & チャンク索引の構築
    bool Load(const char* path);
    void Unload();

    bool IsLoaded() const { return !m_Data.empty(); }
    const std::string& Path() const { return m_Path; }
    const uint8_t* Data() const { return m_Data.empty() ? nullptr : m_Data.data(); }
    uint32_t Size() const { return (uint32_t)m_Data.size(); }

    const std::vector<DatChunk>& Chunks() const { return m_Chunks; }

    // 名前（4 文字）で線形探索。AltanaView の FUN_00405900 と同じ「最初の一致」。
    //  type に DatChunk::TYPE_ANY 以外を指定すると型でも絞り込む。
    static const uint32_t TYPE_ANY = 0xFFFFFFFFu;
    const DatChunk* Find(const char* name4, uint32_t type = TYPE_ANY) const;
    const DatChunk* FindByType(uint32_t type) const;

    // 範囲チェック付きアクセサ
    const uint8_t* At(uint32_t offset) const
    {
        if (!IsLoaded() || offset >= Size()) return nullptr;
        return m_Data.data() + offset;
    }
    bool InRange(uint32_t offset, uint32_t need) const
    {
        return IsLoaded() && offset <= Size() && need <= Size() - offset;
    }

    template <class T> const T* As(uint32_t offset) const
    {
        if (!InRange(offset, (uint32_t)sizeof(T))) return nullptr;
        return reinterpret_cast<const T*>(m_Data.data() + offset);
    }

private:
    std::string            m_Path;
    std::vector<uint8_t>   m_Data;
    std::vector<DatChunk>  m_Chunks;
};
