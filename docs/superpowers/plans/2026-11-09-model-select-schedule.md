# モデル/エフェクト選択 → Schedule 表示 実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** List で項目を単一選択すると DAT を読み、`0x07`（Schedule）を全件列挙して任意のものを再生できるようにする。併せて `/0x07` の読み取り位置を `+0x50` に統一し、generator の `+0xAC` からテクスチャを割り当てる。

**Architecture:** `EffectContext` の構築位置を `+0x50` 固定にし、`GeneratorBlock.linkName`（`+0xAC`）から `0x20` を解決して `MotionInstance.texIndex` に渡す。UI は `App.cpp` の List パネルに Schedule LISTBOX を追加し、選択を 7 コマのデバウンスで確定 → `BuildFromChunk(name)` で再生する。検証は既存の headless CLI（`--selftest` / `--action` / `--shot`）と外部スクリプトで行う。

**Tech Stack:** C++17 / Win32 / DirectX 11 / MSBuild v143 (Release x64) / PowerShell 5.1（検証スクリプト）

## Global Constraints

- 描画 API は **DirectX 11（D3D11）**。`d3d11.lib` / `dxgi.lib` / `d3dcompiler.lib`、v143 / C++17 / Windows SDK 10.0（変更しない）
- **60 fps 前提**。待ち時間は**コマ数**で表す（`7 コマ = 116.7 ms`）。`SetTimer` / `WM_TIMER` は使わない
- **新規 CLI フラグを追加しない**（`--list` / `--base` / `--action` / `--selftest` / `--shot` の 5 本のみ）
- `0x07` の命令列は **`+0x50` 固定**（`len == 0` で終端）
- 用語は **Schedule**（`0x07`）。既存の実装名（`ActionProgram` / `--action`）は変更しない
- 作業ブランチは `design/list-to-screen` から切った `feature/schedule` を使い、**小さな日本語コミット**を積む（`main` へ直接コミットしない）
- ユーザーの明示指示なく `main` へマージしない

---

## File Structure

| ファイル | 役割 | 変更 |
|---|---|---|
| `Effect.h` | `GeneratorBlock.linkName`、`EffectContext::ProgStart()` / `FindTextureIndex()` | 追加 |
| `Effect.cpp` | `+0x50` 固定、`+0xAC` 読み取り、テクスチャ解決、Spawn の texIndex | 変更 |
| `App.cpp` | Schedule LISTBOX、7 コマ確定、`--selftest` に `prog=` / `tex=` | 変更 |
| `tools/EffectRes.ps1` | 必要資源の外部オラクル（DAT 直接解析） | 新規 |
| `ReadMe.md` | 操作説明（単一選択・Schedule） | 変更 |

---

### Task 1: `0x07` の命令列を `+0x50` に統一

**Files:**
- Modify: `Effect.cpp:37-77`（`DetectProgramStart` を削除）
- Modify: `Effect.cpp:129-145`（`progStart` の決定）
- Modify: `Effect.h:6`（コメントの `+0x80` を `+0x50`）

**Interfaces:**
- Consumes: `DatFile::Find` / `DatChunk`（既存）
- Produces: `EffectContext::Build` / `BuildFromChunk` が常に `+0x50` から読む（後続タスクは前提にするだけ）

- [ ] **Step 1: 失敗する現状を確認**

```powershell
cd D:\projects\EffectTest
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\18.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'timeline'
```
Expected（現状＝失敗）: `timeline  = 0 insts, duration=0.0000s`

- [ ] **Step 2: `DetectProgramStart` を削除し、`+0x50` 固定に置き換える**

`Effect.cpp` の `DetectProgramStart`（`//  0x07 ブロックの命令列開始オフセットを自動判定する` から関数末尾まで）を**丸ごと削除**し、`BuildInternal` の該当箇所を次へ置き換える。

```cpp
    if (mainChunk != nullptr)
    {
        // 0x07 の命令列は +0x50 から始まる（Effect / NPC 共通。解析 §8）
        //   実測: 1,848 チャンク中 1,806（97.7%）が +0x50 から len==0 で正常終端
        const uint32_t progStart = 0x50;
        if (mainChunk->size <= progStart)
        {
            Reset();
            return !m_Generators.empty();
        }

        const uint32_t start = mainChunk->offset + progStart;  // 命令列の開始
        const uint32_t end   = mainChunk->End();
```

（以降の命令読み取りループは現状のまま）

- [ ] **Step 3: コメントを実測に合わせる**

`Effect.h:6` を次へ変更。

```cpp
//    timeline (type 0x07)    : 命令列（+0x50 起点、len==0 で終端）
```

- [ ] **Step 4: ビルド**

```powershell
cd D:\projects\EffectTest
msbuild EffectTest.sln /p:Configuration=Release /p:Platform=x64
```
Expected: `Build succeeded`（エラー 0）

- [ ] **Step 5: 修正後の値を確認**

```powershell
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\18.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'timeline'
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\17.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'timeline'
```
Expected:
- `11/18`: `timeline  = 23 insts, duration=4.1667s`
- `11/17`: `timeline  = 22 insts, duration=3.3333s`（`duration` は prologue の delay=0 のため不変）

- [ ] **Step 6: コミット**

```powershell
git add Effect.cpp Effect.h
git commit -m "fix: 0x07 の命令列を +0x50 固定に統一（main の +0x80 特例を廃止）"
```

---

### Task 2: `--selftest` に `tex=N` を出す（この時点では常に -1）

**Files:**
- Modify: `App.cpp:894-898`（`RunSelfTest` のインスタンス行）

**Interfaces:**
- Consumes: `MotionInstance::texIndex`（既存。`Effect.cpp:288` で `m_Textures.empty() ? -1 : 0`）
- Produces: `--selftest` の各行に `tex=<int>`（Task 3 で値が変わる）

- [ ] **Step 1: 失敗を確認**

```powershell
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\17.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'tex='
```
Expected（現状）: 出力なし

- [ ] **Step 2: 出力に `tex=%d` を追加**

`App.cpp` の `RunSelfTest` 内、フレーム行の `fprintf` を次のとおり置き換える。

```cpp
                fprintf(fp,
                        " | %s t=%.3f/%.3f pos=(%.3f,%.3f,%.3f) scl=(%.3f,%.3f,%.3f) col=(%.2f,%.2f,%.2f,%.2f) tex=%d",
                        mi.gen->chunk.name, mi.sub.Elapsed(), mi.gen->Length(),
                        p[0], p[1], p[2], s[0], s[1], s[2], c[0], c[1], c[2], c[3],
                        mi.texIndex);
```

- [ ] **Step 3: ビルドして確認**

```powershell
msbuild EffectTest.sln /p:Configuration=Release /p:Platform=x64
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\17.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'tex=' | Select-Object -First 3
```
Expected: `... tex=-1` が並ぶ（未実装のため）

- [ ] **Step 4: コミット**

```powershell
git add App.cpp
git commit -m "feat: --selftest のインスタンス行に tex= を追加（割当検証の観測点）"
```

---

### Task 3: `+0xAC` のリンク名からテクスチャを割り当てる

**Files:**
- Modify: `Effect.h:95-102`（`GeneratorBlock` に `linkName`）
- Modify: `Effect.h:158-159`（`FindTextureIndex` の宣言）
- Modify: `Effect.cpp:102-107`（`+0xAC` 読み取り）
- Modify: `Effect.cpp:282-288`（`Spawn` のテクスチャ決定）
- 追加: `Effect.cpp` に `FindTextureIndex` 実装（`FindGenerator` の直後）

**Interfaces:**
- Produces: `int EffectContext::FindTextureIndex(const char* name4) const`（見つからなければ `-1`）
- Consumes: `TimTexture::name`（`Tim.h:16`。`0x20` チャンクの 4 文字名）

- [ ] **Step 1: 失敗する現状を確認**

```powershell
Select-String -Path effecttest_selftest.txt -Pattern 'tex=-1' | Measure-Object | Select-Object -ExpandProperty Count
```
Expected: 1 以上（すべて `-1`＝テクスチャ未割当）

- [ ] **Step 2: `GeneratorBlock` に `linkName` を追加**

`Effect.h` の `GeneratorBlock` 末尾（`uint8_t flag = 0;` の直後）に追加。

```cpp
    char     linkName[5] = {};  // +0xAC の 4 文字リンク名（0x20/0x1F/0x21/0x3D/base/予約語）
```

- [ ] **Step 3: `FindTextureIndex` を宣言**

`Effect.h` の公開部（`const GeneratorBlock* FindGenerator(...)` の直後）に追加。

```cpp
    int FindTextureIndex(const char* name4) const;
```

- [ ] **Step 4: `+0xAC` を読み取る**

`Effect.cpp` の generator 索引ループ、`g.flag = p[0x79];` の直後に追加。

```cpp
        if (c.size > 0xB0)
        {
            memcpy(g.linkName, p + 0xAC, 4);
            g.linkName[4] = '\0';
        }
```

- [ ] **Step 5: 解決関数を実装**

`Effect.cpp` の `FindGenerator` の直後に追加。

```cpp
// 0x07 の 0x05 generator が持つ +0xAC のリンク名 → 0x20 テクスチャ番号
//   見つからなければ -1（呼び出し側で白テクスチャにフォールバック）
int EffectContext::FindTextureIndex(const char* name4) const
{
    if (name4 == nullptr || name4[0] == '\0') return -1;
    for (size_t i = 0; i < m_Textures.size(); ++i)
    {
        const std::string& n = m_Textures[i].name;
        if (n.size() >= 4 && memcmp(n.c_str(), name4, 4) == 0) return (int)i;
    }
    return -1;
}
```

- [ ] **Step 6: `Spawn` で割り当てる**

`Effect.cpp:287-288` を次のとおり置き換える。

```cpp
    MotionInstance mi;
    const int tex = FindTextureIndex(g->linkName);
    mi.Reset(m_Dat, g, startSec, tex, op);
```

- [ ] **Step 7: ビルドして確認**

```powershell
msbuild EffectTest.sln /p:Configuration=Release /p:Platform=x64
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\17.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'tex=' | Select-Object -First 6
```
Expected: `tex=0` / `tex=1` / `tex=2` が現れる（`fai0`=0, `fai1`=1, `fai2`=2。`-1` ではない）

- [ ] **Step 8: 別 DAT でも確認**

```powershell
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\22.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'tex=' | Select-Object -First 6
```
Expected: `pka1`/`kem2` に対応する番号（`-1` ではない）。`0x1F` 依存（`ic`）の generator は `-1`（白）でよい

- [ ] **Step 9: コミット**

```powershell
git add Effect.h Effect.cpp
git commit -m "feat: generator の +0xAC から 0x20 テクスチャを割り当てる（未解決は白）"
```

---

### Task 4: `--selftest` に `prog=0x50` を出す

**Files:**
- Modify: `Effect.h:147-160`（getter 追加）、`Effect.h:166-182`（メンバ追加）
- Modify: `Effect.cpp:79-88`（構築時に保存）
- Modify: `App.cpp:853-856`（`--selftest` ヘッダ出力）

**Interfaces:**
- Produces: `uint32_t EffectContext::ProgStart() const`（Task 1 の `progStart` を公開）

- [ ] **Step 1: 失敗を確認**

```powershell
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\17.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'prog'
```
Expected（現状）: 出力なし

- [ ] **Step 2: getter / メンバを追加**

`Effect.h` の公開部（`size_t   Ip() ...` の並び）に追加。

```cpp
    uint32_t ProgStart() const { return m_ProgStart; }
```

同ファイルの private メンバ（`size_t   m_Ip = 0;` の直前）に追加。

```cpp
    uint32_t m_ProgStart = 0;
```

- [ ] **Step 3: 構築時に保存**

`Effect.cpp` の `BuildInternal` 冒頭（`m_Duration = 0.0f;` の直後）に追加。

```cpp
    m_ProgStart = 0;
```

Task 1 の `const uint32_t progStart = 0x50;` の直後に追加。

```cpp
        m_ProgStart = progStart;
```

- [ ] **Step 4: `--selftest` に 1 行追加**

`App.cpp:856`（`fprintf(fp, "generators= %zu\n", ...)` の直後）に追加。

```cpp
    fprintf(fp, "prog      = 0x%X\n", eff.ProgStart());
```

- [ ] **Step 5: ビルドして確認**

```powershell
msbuild EffectTest.sln /p:Configuration=Release /p:Platform=x64
.\x64\Release\EffectTest.exe --selftest "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\17.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'prog'
```
Expected: `prog      = 0x50`

- [ ] **Step 6: コミット**

```powershell
git add Effect.h Effect.cpp App.cpp
git commit -m "feat: --selftest に prog=0x50 を出力（開始オフセットの検証用）"
```

---

### Task 5: Schedule LISTBOX と単一選択（Effect も `0x07` 全件）

**Files:**
- Modify: `App.cpp:64-80`（グローバル・enum）
- Modify: `App.cpp:93-98`（前方宣言）
- Modify: `App.cpp:524-555`（`LayoutPanel` / `BuildPanelControls`）
- Modify: `App.cpp:575-594`（`FillItems`）
- Modify: `App.cpp:599-669`（`LoadListSelection` / `PlaySelectedAction` を置換）
- Modify: `App.cpp:686-704`（`PanelProc`）
- Modify: `App.cpp:1180-1185`（メインループ）

**Interfaces:**
- Consumes: `EffectContext::BuildFromChunk`（既存）、`ListGroup::isEffect`（`ListDb.h:29`）
- Produces: `CommitSelection(int)` / `PlaySelectedSchedule(int)` / `TickPendingCommit()`（`App.cpp` 内 static）

- [ ] **Step 1: グローバルと enum を置き換える**

`App.cpp` の List ブラウザ用グローバル（`g_hGroup` 〜 `g_ActionNames`）を次へ置き換える。

```cpp
    ListDb   g_List;
    char     g_FfxiBase[MAX_PATH] = {};
    HWND     g_hPanel         = nullptr;
    HWND     g_hGroup         = nullptr;
    HWND     g_hItems         = nullptr;
    HWND     g_hSchedule      = nullptr;
    HWND     g_hScheduleLabel = nullptr;

    EffectContext            g_ActionCtx;          // NPC の 0x07 用
    bool                     g_ActionActive = false;
    std::vector<std::string> g_ScheduleNames;

    int  g_PendingItem    = -1;    // 確定待ちの項目 index
    int  g_PendingFrames  = 0;     // 静止コマ数
    int  g_CommittedItem  = -1;
    int  g_CommittedGroup = -1;
    bool g_CommitBusy     = false;

    const int kCommitDelayFrames = 7;   // 60 fps で 116.7 ms（設計 §4.7.1 D4）

    EffectBase               g_Base;

    const wchar_t* kPanelClass = L"EffectTestPanel";

    enum : int { IDC_GROUP = 1001, IDC_ITEMS = 1002, IDC_SCHEDULE = 1003, IDC_SCHEDULE_LABEL = 1004 };
```

- [ ] **Step 2: 前方宣言を更新**

```cpp
static void BuildPanelControls(HWND hPanel);
static void FillGroups();
static void FillItems();
static void CommitSelection(int itemIndex);
static void PlaySelectedSchedule(int index);
static void TickPendingCommit();
static void ScheduleClear();
static void ClearSelection();
static void UpdateScheduleLabel(int count);
static LRESULT CALLBACK PanelProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
```

- [ ] **Step 3: ヘルパ 3 つを追加（`LayoutPanel` の直前）**

```cpp
static void UpdateScheduleLabel(int count)
{
    if (g_hScheduleLabel == nullptr) return;
    wchar_t buf[64];
    if (count < 0) wcscpy_s(buf, L"Schedule  (-)");
    else           swprintf_s(buf, L"Schedule  (%d)", count);
    SetWindowTextW(g_hScheduleLabel, buf);
}

static void ScheduleClear()
{
    if (g_hSchedule != nullptr)
    {
        SendMessageW(g_hSchedule, LB_RESETCONTENT, 0, 0);
        EnableWindow(g_hSchedule, FALSE);
    }
    g_ScheduleNames.clear();
    UpdateScheduleLabel(-1);
}

static void ClearSelection()
{
    g_Effect.ReleaseTextures();
    g_Effect.Reset();
    g_ActionCtx.Reset();
    g_ActionActive = false;
    ScheduleClear();
    g_Dat.Unload();
    g_DatPath.clear();
    g_CommittedItem = -1;
    g_CommittedGroup = -1;
    g_PendingItem = -1;
    g_PendingFrames = 0;
    UpdateTitle();
}
```

- [ ] **Step 4: `LayoutPanel` を置き換える**

```cpp
static void LayoutPanel(HWND hPanel)
{
    RECT rc = {};
    GetClientRect(hPanel, &rc);
    const int w = rc.right - 16;
    if (w < 80) return;

    const int bottom = rc.bottom;
    const int schedH = (bottom < 320) ? 96 : 128;
    const int labelH = 18;

    MoveWindow(g_hGroup, 8, 8, w, 200, TRUE);
    MoveWindow(g_hSchedule, 8, bottom - 8 - schedH, w, schedH, TRUE);
    MoveWindow(g_hScheduleLabel, 8, bottom - 8 - schedH - labelH - 4, w, labelH, TRUE);

    int itemsH = bottom - 8 - schedH - labelH - 4 - 8 - 40;
    if (itemsH < 64) itemsH = 64;
    MoveWindow(g_hItems, 8, 40, w, itemsH, TRUE);
}
```

- [ ] **Step 5: `BuildPanelControls` を置き換える**

```cpp
static void BuildPanelControls(HWND hPanel)
{
    const HINSTANCE hi = (HINSTANCE)GetWindowLongPtrA(hPanel, GWLP_HINSTANCE);

    g_hGroup = CreateWindowExW(0, L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                               8, 8, 320, 240, hPanel, (HMENU)IDC_GROUP, hi, nullptr);

    g_hItems = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                               8, 40, 320, 320, hPanel, (HMENU)IDC_ITEMS, hi, nullptr);

    g_hScheduleLabel = CreateWindowExW(0, L"STATIC", L"Schedule  (-)",
                                       WS_CHILD | WS_VISIBLE,
                                       8, 400, 320, 18, hPanel, (HMENU)IDC_SCHEDULE_LABEL, hi, nullptr);

    g_hSchedule = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                  8, 420, 320, 128, hPanel, (HMENU)IDC_SCHEDULE, hi, nullptr);
    EnableWindow(g_hSchedule, FALSE);

    FillGroups();
    LayoutPanel(hPanel);
}
```

- [ ] **Step 6: `FillItems` の先頭を更新**

`FillItems` の `LB_RESETCONTENT` 〜 `g_ActionActive = false;` を次へ置き換える。

```cpp
    SendMessageW(g_hItems, LB_RESETCONTENT, 0, 0);
    ScheduleClear();
    g_ActionActive = false;
    g_PendingItem = -1;
    g_PendingFrames = 0;
    g_CommittedItem = -1;
    g_CommittedGroup = -1;
```


- [ ] **Step 7: `LoadListSelection` を `CommitSelection` に置き換える**

`App.cpp:596-643` の `LoadListSelection` を**丸ごと削除**し、次を追加する。

```cpp
// 項目の確定（唯一の経路）。Effect / NPC 共通で 0x07 を全件列挙する。
static void CommitSelection(int itemIndex)
{
    const int gi = (int)SendMessageA(g_hGroup, CB_GETCURSEL, 0, 0);
    if (gi < 0 || g_CommitBusy) return;
    if (gi == g_CommittedGroup && itemIndex == g_CommittedItem) return;

    const ListGroup& g = g_List.Group((size_t)gi);
    const std::vector<ListItem>& items = g_List.Items((size_t)gi);
    if (itemIndex < 0 || itemIndex >= (int)items.size()) return;

    g_CommitBusy = true;

    char path[MAX_PATH] = {};
    if (!ListDb::ResolvePath(g_FfxiBase, g, items[itemIndex], path, MAX_PATH))
    {
        Log("[schedule] clear: resolve failed %s", items[itemIndex].id.c_str());
        ClearSelection();
        g_CommitBusy = false;
        return;
    }
    if (!LoadDatFile(path))
    {
        Log("[schedule] clear: load failed %s", path);
        ClearSelection();
        g_CommitBusy = false;
        return;
    }

    // 0x07 を全件列挙（Effect / NPC 共通）
    ScheduleClear();
    for (const DatChunk& c : g_Dat.Chunks())
        if (c.type == 0x07) g_ScheduleNames.push_back(c.name);
    for (const std::string& n : g_ScheduleNames)
        SendMessageW(g_hSchedule, LB_ADDSTRING, 0, (LPARAM)Widen(n).c_str());

    int def = 0;
    for (size_t i = 0; i < g_ScheduleNames.size(); ++i)
        if (g_ScheduleNames[i] == "main") { def = (int)i; break; }
    if (!g_ScheduleNames.empty()) SendMessageW(g_hSchedule, LB_SETCURSEL, def, 0);
    EnableWindow(g_hSchedule, TRUE);
    UpdateScheduleLabel((int)g_ScheduleNames.size());
    Log("[schedule] count=%zu", g_ScheduleNames.size());

    // Effect は既定 Schedule（main）を構築して即再生。NPC は表示のみ（P1）
    if (g.isEffect && !g_ScheduleNames.empty())
    {
        PlaySelectedSchedule(def);
        g_Paused = false;
    }

    g_CommittedGroup = gi;
    g_CommittedItem  = itemIndex;
    UpdateTitle();
    g_CommitBusy = false;
}
```

- [ ] **Step 8: `PlaySelectedAction` を `PlaySelectedSchedule` に置き換える**

`App.cpp:648-669` を**丸ごと削除**し、次を追加する。

```cpp
// Schedule の選択＝即再生（Effect は g_Effect、NPC は g_ActionCtx）
static void PlaySelectedSchedule(int index)
{
    if (index < 0 || index >= (int)g_ScheduleNames.size()) return;
    if (!g_Dat.IsLoaded()) return;

    const std::string& nm = g_ScheduleNames[index];
    const int gi = (int)SendMessageA(g_hGroup, CB_GETCURSEL, 0, 0);
    const bool isEffect = (gi >= 0) && g_List.Group((size_t)gi).isEffect;

    if (isEffect)
    {
        if (!g_Effect.BuildFromChunk(g_Dat, nm.c_str()))
        {
            Log("[schedule] %s : timeline 無し", nm.c_str());
            return;
        }
        g_Effect.BuildTextures(GfxDevice());
        g_Effect.Reset();
        g_ActionActive = false;
        Log("[schedule] effect %s insts=%zu dur=%.3fs", nm.c_str(),
            g_Effect.Timeline().size(), g_Effect.Duration());
        return;
    }

    if (!g_ActionCtx.BuildFromChunk(g_Dat, nm.c_str()))
    {
        Log("[schedule] %s : timeline 無し", nm.c_str());
        return;
    }
    g_ActionCtx.Reset();
    g_ActionActive = true;
    Log("[schedule] npc %s insts=%zu", nm.c_str(), g_ActionCtx.Timeline().size());
}
```

- [ ] **Step 9: 7 コマ確定を追加（`PlaySelectedSchedule` の直後）**

```cpp
// 描画ループから毎フレーム呼ぶ。選択が 7 コマ静止したら確定する。
static void TickPendingCommit()
{
    if (g_PendingItem < 0) return;
    if (++g_PendingFrames < kCommitDelayFrames) return;

    const int item = g_PendingItem;
    g_PendingItem = -1;
    g_PendingFrames = 0;
    CommitSelection(item);
}
```

- [ ] **Step 10: `PanelProc` の `WM_COMMAND` を置き換える**

```cpp
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_GROUP:
            if (HIWORD(wp) == CBN_SELCHANGE) FillItems();
            return 0;

        case IDC_ITEMS:
            if (HIWORD(wp) == LBN_SELCHANGE || HIWORD(wp) == LBN_DBLCLK)
            {
                g_PendingItem = (int)SendMessageA(g_hItems, LB_GETCURSEL, 0, 0);
                g_PendingFrames = 0;
            }
            return 0;

        case IDC_SCHEDULE:
            if (HIWORD(wp) == LBN_SELCHANGE || HIWORD(wp) == LBN_DBLCLK)
                PlaySelectedSchedule((int)SendMessageA(g_hSchedule, LB_GETCURSEL, 0, 0));
            return 0;
        }
        return 0;
```

- [ ] **Step 11: メインループで確定を消化する**

`App.cpp` の `if (!g_Paused && g_ActionActive) g_ActionCtx.Advance(g_Dt);` の直後に追加。

```cpp
        TickPendingCommit();
```

- [ ] **Step 12: ビルド（警告 0 を目標）**

```powershell
msbuild EffectTest.sln /p:Configuration=Release /p:Platform=x64
```
Expected: `Build succeeded`

- [ ] **Step 13: 手動確認（GUI）**

```powershell
.\x64\Release\EffectTest.exe
```
操作: `B` でパネル表示 → グループ `Black Magic` → 項目 `Fire II (11/18)` を**シングルクリック**。
Expected: ラベルが `Schedule  (1)`、リストに `main`、タイトルに `11\18.DAT`。次に `Song` → `Foe Requiem (12/43)` でラベル `Schedule  (2)`、`main` と `hit1` が並び、`hit1` を選ぶと再生が変わる。
ログ（`effecttest_log.txt`）に `[schedule] count=2` と `[schedule] effect hit1 insts=15` が出る。

- [ ] **Step 14: コミット**

```powershell
git add App.cpp
git commit -m "feat: Schedule LISTBOX を追加（Effect/NPC 共通で 0x07 全件、7 コマ確定、main 既定）"
```


---

### Task 6: 必要資源の外部オラクル `tools/EffectRes.ps1`

**Files:**
- Create: `tools/EffectRes.ps1`
- Create: `tools/README.md`

**Interfaces:**
- Produces: `[res] SUMMARY dat=<name> prog=0x50 insts=<n> gen=<u>/<g> tex=<u>/<t> base=<b> unused=<n>`
- Consumes: DAT ファイル（アプリ非依存。`0x07` は `+0x50`、`+0xAC` はリンク名）

- [ ] **Step 1: スクリプトを作成**

`tools/EffectRes.ps1` を新規作成（全文）。

```powershell
param(
    [Parameter(Mandatory = $true)][string]$Dat,
    [string]$Base = 'C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\0\0.DAT'
)

$genOps = @(0x02, 0x03, 0x05, 0x0A, 0x0B, 0x1C, 0x4A, 0x53, 0x60)

function Get-Chunks([byte[]]$b) {
    $n = New-Object System.Collections.ArrayList; $t = New-Object System.Collections.ArrayList
    $o = New-Object System.Collections.ArrayList; $s = New-Object System.Collections.ArrayList
    $pos = 0
    while ($pos + 16 -le $b.Length) {
        $hdr = [uint32][BitConverter]::ToUInt32($b, $pos + 4)
        $next = [int](([uint64]$hdr -shr 3) -band 0x7FFFF0)
        if ($next -lt 16 -or ($pos + $next) -gt $b.Length) { break }
        $nm = -join ([byte[]]$b[$pos..($pos + 3)] | ForEach-Object { if ($_ -ge 32 -and $_ -le 126) { [char]$_ } else { '.' } })
        $null = $n.Add($nm); $null = $t.Add([int]($hdr -band 0x7F)); $null = $o.Add($pos); $null = $s.Add($next)
        $pos += $next
    }
    return [pscustomobject]@{ N = $n; T = $t; O = $o; S = $s }
}

function Get-Name4([byte[]]$b, [int]$off) {
    return -join ([byte[]]$b[$off..($off + 3)] | ForEach-Object { if ($_ -ge 32 -and $_ -le 126) { [char]$_ } else { '.' } })
}

$bytes = [IO.File]::ReadAllBytes($Dat)
$c = Get-Chunks $bytes
$main = -1
for ($i = 0; $i -lt $c.N.Count; $i++) { if ($c.T[$i] -eq 0x07 -and $c.N[$i] -eq 'main') { $main = $i; break } }
if ($main -lt 0) { for ($i = 0; $i -lt $c.N.Count; $i++) { if ($c.T[$i] -eq 0x07) { $main = $i; break } } }
if ($main -lt 0) { Write-Output "[res] ERROR 0x07 が無い"; exit 2 }

$refs = @{}; $insts = 0; $spawn = 0
$pos = $c.O[$main] + 0x50; $end = $c.O[$main] + $c.S[$main]
while ($pos + 8 -le $end -and $insts -lt 4096) {
    $op = [int]$bytes[$pos]; $len = [int]$bytes[$pos + 1] -band 0x1F
    if ($len -eq 0) { break }
    $insts++
    if ($genOps -contains $op) { $spawn++; $refs[(Get-Name4 $bytes ($pos + 8))] = 1 }
    $pos += $len * 4
}

$base20 = @{}; $base05 = @{}
if (Test-Path $Base) {
    $bb = [IO.File]::ReadAllBytes($Base); $bc = Get-Chunks $bb
    for ($i = 0; $i -lt $bc.N.Count; $i++) {
        if ($bc.T[$i] -eq 0x20) { $base20[$bc.N[$i]] = 1 }
        if ($bc.T[$i] -eq 0x05) { $base05[$bc.N[$i]] = 1 }
    }
}

$gens = 0; $genUsed = 0; $texNames = @{}
for ($i = 0; $i -lt $c.N.Count; $i++) {
    if ($c.T[$i] -eq 0x05) { $gens++; if ($refs.ContainsKey($c.N[$i])) { $genUsed++ } }
    if ($c.T[$i] -eq 0x20) { $texNames[$c.N[$i]] = 1 }
}

$used = @(); $baseTex = @(); $unused = @()
$nTex = 0; $nOther = 0; $nEmpty = 0; $nBase = 0; $nKw = 0; $nUnres = 0
for ($i = 0; $i -lt $c.N.Count; $i++) {
    if ($c.T[$i] -ne 0x05) { continue }
    $off = $c.O[$i] + 0xAC
    if ($off + 4 -gt $bytes.Length) { continue }
    $link = Get-Name4 $bytes $off
    if ($refs.ContainsKey($c.N[$i])) {
        if ($texNames.ContainsKey($link)) { $nTex++; if ($used -notcontains $link) { $used += $link } }
        elseif ($base20.ContainsKey($link)) { $nBase++; if ($baseTex -notcontains $link) { $baseTex += $link } }
        elseif ($link -eq 'dist') { $nKw++ }
        elseif ($link -match '^\.+$') { $nEmpty++ }
        elseif ($base05.ContainsKey($link)) { $nOther++ }
        else { $nUnres++ }
    }
}
foreach ($k in $texNames.Keys) { if ($used -notcontains $k) { $unused += $k } }

$name = Split-Path $Dat -Leaf
Write-Output ("[res] dat={0} chunks={1} gen={2} tex={3}" -f $name, $c.N.Count, $gens, $texNames.Count)
Write-Output ("[res] prog=0x50 insts={0} spawn={1}" -f $insts, $spawn)
Write-Output ("[res] gens={0}/{1}" -f $genUsed, $gens)
Write-Output ("[res] tex={0}/{1} ({2})" -f $used.Count, $texNames.Count, ($used -join ','))
Write-Output ("[res] base_tex={0} ({1})" -f $baseTex.Count, ($baseTex -join ','))
Write-Output ("[res] unused={0} ({1})" -f $unused.Count, ($unused -join ','))
Write-Output ("[res] link=tex:{0} other:{1} empty:{2} base:{3} keyword:{4} unresolved:{5}" -f $nTex, $nOther, $nEmpty, $nBase, $nKw, $nUnres)
Write-Output ("[res] SUMMARY dat={0} prog=0x50 insts={1} gen={2}/{3} tex={4}/{5} base={6} unused={7}" -f $name, $insts, $genUsed, $gens, $used.Count, $texNames.Count, $baseTex.Count, $unused.Count)
```

`tools/README.md` を新規作成。

```markdown
# tools

- `EffectRes.ps1 <dat> [-Base <0/0.DAT>]` : エフェクト DAT の Schedule 命令列と必要資源を出力する（アプリ非依存の検証用オラクル）。
```

- [ ] **Step 2: ゴールデンと一致することを確認**

```powershell
cd D:\projects\EffectTest
.\tools\EffectRes.ps1 -Dat "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\17.DAT"
.\tools\EffectRes.ps1 -Dat "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\18.DAT"
```
Expected（SUMMARY 行）:
- `11/17`: `insts=22 gen=8/8 tex=3/5 base=2 unused=2`
- `11/18`: `insts=23 gen=11/11 tex=3/7 base=1 unused=4`

- [ ] **Step 3: コミット**

```powershell
git add tools/EffectRes.ps1 tools/README.md
git commit -m "feat: 必要資源の検証用オラクル tools/EffectRes.ps1 を追加"
```


---

### Task 7: ReadMe の操作説明と検証コマンド

**Files:**
- Modify: `ReadMe.md`

**Interfaces:** なし（文書のみ）

- [ ] **Step 1: 操作説明を追記**

`ReadMe.md` の「使い方」節に次を追記する（「ダブルクリックで確定」の記述があれば削除）。

```markdown
- List は **単一選択**で確定します（ダブルクリックも同じ）。
- 3 段目は **Schedule**（DAT 内の `0x07` 全件）。既定は `main`。選ぶと即再生します。
- 起動時は何も表示しません。`X` 座標軸 / `B` パネル / `Space` 停止 / `R` リセット。
```

- [ ] **Step 2: 検証コマンドを追記**

`ReadMe.md` の「検証」節に追記。

```markdown
- `--selftest <dat>` は `prog=0x50` と各インスタンスの `tex=` を出力します（開始位置・テクスチャ割当の回帰）。
- `tools\EffectRes.ps1 <dat>` は必要資源（generator / texture / base）を独立に算出します。
```

- [ ] **Step 3: コミット**

```powershell
git add ReadMe.md
git commit -m "docs: ReadMe の操作説明と検証コマンドを更新（単一選択・Schedule・prog/tex）"
```

---

## 検証計画（全タスク完了後）

| # | コマンド | 期待値 |
|---|---|---|
| V1 | `--selftest "…\11\17.DAT"` | `prog      = 0x50` / `timeline  = 22 insts, duration=3.3333s` / インスタンス行に `tex=0,1,2` |
| V2 | `--selftest "…\11\18.DAT"` | `timeline  = 23 insts, duration=4.1667s`（修正前は 0 insts） |
| V3 | `--selftest "…\11\22.DAT"` | `timeline  = 23 insts` / `tex=` に `pka1`/`kem2` の番号 |
| V4 | `--action "…\131\6.DAT"` | `0x07` 一覧が 39 件（NPC 回帰。出力書式は不変） |
| V5 | `tools\EffectRes.ps1 -Dat "…\11\17.DAT"` | `insts=22 gen=8/8 tex=3/5 base=2 unused=2` |
| V6 | `tools\EffectRes.ps1 -Dat "…\11\18.DAT"` | `insts=23 gen=11/11 tex=3/7 base=1 unused=4` |
| V7 | `--shot "…\11\17.DAT" out_fire.png` | `ok=1`（非背景 > 6,000 は目視/画素検査） |
| V8 | GUI 手動 | `Song` → `12/43` で `Schedule (2)`、`hit1` を選ぶと再生が変わる |

**未達時の切り分け**: V1/V2/V3 が失敗 → Task 1（`+0x50`）。`tex=-1` のまま → Task 3。V5/V6 の不一致 → Task 6 のスクリプト。

---

## セルフレビュー（計画作成者による確認）

- **仕様カバレッジ**: 設計 §4.7（UI・確定・既定 `main`）→ Task 5／§4.8.2（`+0x50`）→ Task 1／§4.8.3（`+0xAC`）→ Task 3／§4.8.5（検証）→ Task 2・4・6／§4.9（カタログのゴールデン）→ Task 6／§4.7.7 の ReadMe 更新 → Task 7
- **意図的な非対象**: §4.8.4 の base 遅延 SRV（base テクスチャは当面 `-1`＝白で継続）／サブ Schedule の同時再生／`0x1F`・`0x21` の描画
- **プレースホルダ**: なし（各ステップに実コード・実コマンド・期待値を記載）
- **型の一貫性**: `ProgStart()` / `FindTextureIndex(const char*)` / `GeneratorBlock::linkName` / `kCommitDelayFrames` / `CommitSelection(int)` / `PlaySelectedSchedule(int)` を全タスクで同一名・同一型で使用
- **順序の根拠**: Task 1（読み取り位置）→ 2/3（観測点）→ 4（割当）→ 5（UI）→ 6（外部検証）→ 7（文書）。Task 1〜4 は UI なしで `--selftest` により単独検証できる

