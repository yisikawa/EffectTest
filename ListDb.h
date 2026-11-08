#pragma once
//======================================================================
//  ListDb.h : ModelTest / AltanaView の List フォルダを読む
//
//  ・NPC   : List\NPC_ROM<n>.lst      … "1-131-6,裏ゴブリン" / "3-2,オーク神"
//  ・Effect: List\Effect\index.csv   … "BlackMagic,Black Magic"（3 列目は ROM 番号）
//            List\Effect\<name>.csv  … "11/17,Fire"
//
//  ID から FFXI の DAT パスを解決する（ROM1 は "ROM"、ROM2〜9 は "ROM<n>"）。
//======================================================================
#include <string>
#include <vector>

struct ListItem
{
    std::string  id;      // "11/17" または "1-131-6"（ASCII）
    std::string  name;    // 表示名（UTF-8。ログ用）
    std::wstring wname;   // 表示名（UTF-16。Win32 コントロール用）
    int          rom = 1; // 1..9
};

struct ListGroup
{
    std::string  label;    // コンボに出す名前（UTF-8。ログ用）
    std::wstring wlabel;   // 同上（UTF-16）
    std::string  file;     // List からの相対パス
    int          rom = 1;
    bool         isEffect = false;
};

// 文字コード自動判定（UTF-8 / Shift-JIS）→ UTF-16
std::wstring ListDecodeToWide(const std::string& raw);
// UTF-16 → UTF-8
std::string  ListWideToUtf8(const std::wstring& w);

class ListDb
{
public:
    bool Load(const char* listDir);                       // "<exe>\\List"

    size_t GroupCount() const { return m_Groups.size(); }
    const ListGroup& Group(size_t i) const { return m_Groups[i]; }
    const std::vector<ListItem>& Items(size_t groupIndex); // 遅延ロード

    // アイテム → DAT のフルパス
    static bool ResolvePath(const char* ffxiBase, const ListGroup& g, const ListItem& it,
                            char* out, size_t outSize);

    // "3-2" / "1-131-6" を rom/hi/lo に分解
    static bool ParseNpcId(const std::string& id, int defaultRom, int* rom, int* hi, int* lo);
    // "11/17" を hi/lo に分解
    static bool ParseEffectId(const std::string& id, int* hi, int* lo);

private:
    bool LoadGroup(size_t index);

    std::string m_Dir;
    std::vector<ListGroup> m_Groups;
    std::vector<std::vector<ListItem>> m_Items;
    std::vector<bool> m_Loaded;
};
