#include "ListDb.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    bool ReadAllText(const char* path, std::string& out)
    {
        FILE* fp = nullptr;
        if (fopen_s(&fp, path, "rb") != 0 || fp == nullptr) return false;
        fseek(fp, 0, SEEK_END);
        const long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (n <= 0) { fclose(fp); return false; }
        out.resize((size_t)n);
        const size_t rd = fread(&out[0], 1, (size_t)n, fp);
        fclose(fp);
        out.resize(rd);
        return true;
    }

    // 簡易 UTF-8 妥当性検査
    bool IsValidUtf8(const std::string& s)
    {
        size_t i = 0;
        const size_t n = s.size();
        while (i < n)
        {
            const unsigned char c = (unsigned char)s[i];
            if (c < 0x80) { ++i; continue; }
            int extra = 0;
            if ((c & 0xE0) == 0xC0)      extra = 1;
            else if ((c & 0xF0) == 0xE0) extra = 2;
            else if ((c & 0xF8) == 0xF0) extra = 3;
            else return false;

            if (i + (size_t)extra >= n) return false;
            for (int k = 1; k <= extra; ++k)
                if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
            i += (size_t)extra + 1;
        }
        return true;
    }
}

//======================================================================
//  文字コード変換（UTF-8 / Shift-JIS 自動判定）
//======================================================================
std::wstring ListDecodeToWide(const std::string& raw)
{
    if (raw.empty()) return L"";

    std::string body = raw;
    UINT cp = CP_UTF8;

    if (body.size() >= 3 && (unsigned char)body[0] == 0xEF &&
        (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF)
    {
        body.erase(0, 3);                                  // UTF-8 BOM
    }
    else if (!IsValidUtf8(body))
    {
        cp = 932;                                          // Shift-JIS とみなす
    }

    const int need = MultiByteToWideChar(cp, 0, body.data(), (int)body.size(), nullptr, 0);
    if (need <= 0) return L"";

    std::wstring w((size_t)need, L'\0');
    MultiByteToWideChar(cp, 0, body.data(), (int)body.size(), &w[0], need);
    return w;
}

std::string ListWideToUtf8(const std::wstring& w)
{
    if (w.empty()) return "";
    const int need = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return "";
    std::string s((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], need, nullptr, nullptr);
    return s;
}

namespace
{

    // 1 行取り出す（BOM / CR を除去）
    bool NextLine(const std::string& txt, size_t& cursor, std::string& line)
    {
        if (cursor >= txt.size()) return false;
        size_t e = txt.find('\n', cursor);
        if (e == std::string::npos) e = txt.size();
        line = txt.substr(cursor, e - cursor);
        cursor = e + 1;

        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);                                   // UTF-8 BOM

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        return true;
    }

    void SplitCsv(const std::string& line, std::string* parts, int maxParts)
    {
        int np = 0;
        size_t s = 0;
        while (np < maxParts)
        {
            const size_t c = line.find(',', s);
            if (c == std::string::npos) { parts[np++] = line.substr(s); break; }
            parts[np++] = line.substr(s, c - s);
            s = c + 1;
        }
        for (int i = 0; i < np; ++i)
        {
            std::string& p = parts[i];
            if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
        }
    }

    bool FileExists(const char* p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
}

//======================================================================
bool ListDb::Load(const char* listDir)
{
    m_Dir = (listDir != nullptr) ? listDir : "";
    m_Groups.clear();

    //--------------------------------------------------------------
    // NPC: List\NPC_ROM1.lst 〜 NPC_ROM9.lst
    //--------------------------------------------------------------
    for (int rom = 1; rom <= 9; ++rom)
    {
        char fname[64] = {};
        sprintf_s(fname, "NPC_ROM%d.lst", rom);

        char full[MAX_PATH] = {};
        sprintf_s(full, "%s\\%s", m_Dir.c_str(), fname);
        if (!FileExists(full)) continue;

        ListGroup g;
        g.label    = std::string("NPC  ROM") + std::to_string(rom);
        g.wlabel   = std::wstring(L"NPC  ROM") + std::to_wstring(rom);
        g.file     = fname;
        g.rom      = rom;
        g.isEffect = false;
        m_Groups.push_back(g);
    }

    //--------------------------------------------------------------
    // Effect: List\Effect\index.csv  (file,表示名[,ROM番号])
    //--------------------------------------------------------------
    {
        char idx[MAX_PATH] = {};
        sprintf_s(idx, "%s\\Effect\\index.csv", m_Dir.c_str());

        std::string txt;
        if (ReadAllText(idx, txt))
        {
            size_t cur = 0;
            std::string line;
            while (NextLine(txt, cur, line))
            {
                if (line.empty()) continue;

                std::string parts[3] = {};
                SplitCsv(line, parts, 3);

                ListGroup g;
                g.file     = "Effect\\" + parts[0] + ".csv";
                const std::string disp = parts[1].empty() ? parts[0] : parts[1];
                g.wlabel   = L"Effect: " + ListDecodeToWide(disp);
                g.label    = ListWideToUtf8(g.wlabel);
                g.rom      = parts[2].empty() ? 1 : atoi(parts[2].c_str());
                g.isEffect = true;
                m_Groups.push_back(g);
            }
        }
    }

    m_Items.resize(m_Groups.size());
    m_Loaded.assign(m_Groups.size(), false);
    return !m_Groups.empty();
}

//======================================================================
bool ListDb::LoadGroup(size_t index)
{
    if (index >= m_Groups.size()) return false;
    if (m_Loaded[index]) return true;

    m_Items[index].clear();
    m_Loaded[index] = true;

    char full[MAX_PATH] = {};
    sprintf_s(full, "%s\\%s", m_Dir.c_str(), m_Groups[index].file.c_str());

    std::string txt;
    if (!ReadAllText(full, txt)) return false;

    size_t cur = 0;
    std::string line;
    while (NextLine(txt, cur, line))
    {
        if (line.empty()) continue;

        std::string parts[2] = {};
        SplitCsv(line, parts, 2);
        if (parts[0].empty()) continue;

        ListItem it;
        it.id    = parts[0];
        it.wname = ListDecodeToWide(parts[1].empty() ? parts[0] : parts[1]);
        it.name  = ListWideToUtf8(it.wname);
        it.rom   = m_Groups[index].rom;

        if (!m_Groups[index].isEffect)                 // "rom-hi-lo" 形式なら rom を上書き
        {
            int rom = 0, hi = 0, lo = 0;
            if (ParseNpcId(it.id, m_Groups[index].rom, &rom, &hi, &lo)) it.rom = rom;
        }
        m_Items[index].push_back(it);
    }
    return true;
}

const std::vector<ListItem>& ListDb::Items(size_t groupIndex)
{
    static const std::vector<ListItem> kEmpty;
    if (groupIndex >= m_Groups.size()) return kEmpty;
    LoadGroup(groupIndex);
    return m_Items[groupIndex];
}

//======================================================================
bool ListDb::ParseNpcId(const std::string& id, int defaultRom, int* rom, int* hi, int* lo)
{
    int v[3] = {};
    int n = 0;
    size_t s = 0;
    while (n < 3 && s <= id.size())
    {
        size_t e = id.find('-', s);
        if (e == std::string::npos) e = id.size();
        if (e > s)
        {
            char b[16] = {};
            size_t len = e - s;
            if (len > 15) len = 15;
            memcpy(b, id.c_str() + s, len);
            v[n++] = atoi(b);
        }
        s = e + 1;
    }

    if (n == 3) { *rom = v[0]; *hi = v[1]; *lo = v[2]; return true; }
    if (n == 2) { *rom = defaultRom; *hi = v[0]; *lo = v[1]; return true; }
    return false;
}

bool ListDb::ParseEffectId(const std::string& id, int* hi, int* lo)
{
    const size_t p = id.find('/');
    if (p == std::string::npos) return false;
    *hi = atoi(id.substr(0, p).c_str());
    *lo = atoi(id.substr(p + 1).c_str());
    return true;
}

//======================================================================
bool ListDb::ResolvePath(const char* ffxiBase, const ListGroup& g, const ListItem& it,
                         char* out, size_t outSize)
{
    int rom = g.rom, hi = 0, lo = 0;

    if (g.isEffect)
    {
        if (!ParseEffectId(it.id, &hi, &lo)) return false;
        rom = it.rom;                                   // index.csv の 3 列目
    }
    else
    {
        if (!ParseNpcId(it.id, g.rom, &rom, &hi, &lo)) return false;
    }

    if (rom <= 1) sprintf_s(out, outSize, "%s\\ROM\\%d\\%d.DAT", ffxiBase, hi, lo);
    else          sprintf_s(out, outSize, "%s\\ROM%d\\%d\\%d.DAT", ffxiBase, rom, hi, lo);
    return true;
}

