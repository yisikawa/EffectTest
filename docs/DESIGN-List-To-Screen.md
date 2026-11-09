# List 選択 → 画面表示 設計書（v1.1 / 2026-11-09 改訂）

> 作成: EffectTest / ModelTest の実測に基づく設計提案。実装前のレビュー用。
> この文書は「設計」であり、タスク分解の最終形は実装計画（別紙）で詰める。
>
> **改訂 v1.1（2026-11-09）**: 表示トリガーを**項目の単一選択**へ、3 段目を **Schedule の LISTBOX** へ変更。
> UI と選択フローの詳細（決定事項 D1〜D6・レイアウト・状態・失敗時の扱い）は **§4.7 を正本**とする。

## 0. 前提と用語

- **List** = `ModelTest\List`（`NPC_ROM1..9.lst`、種族別 `.lst`）と `AltanaView\List\Effect`（`index.csv` + カテゴリ CSV）。
  形式は `ID,表示名`（ID は `11/17` または `1-131-6`）。
- **Effect** = エフェクト DAT（`0x07`=`main` タイムライン、`0x05`=ジェネレータ）
- **モデル** = NPC/PC の DAT（`0x2A`=メッシュ、`0x29`=ボーン、`0x2B`=モーション、`0x20`=テクスチャ）
- **Schedule** = モデル DAT 内の `0x07` ブロック（`atk0`, `cast`, `vatk` …）。**AltanaView の Schedule と同義**
  （v1 では「Action」と呼んでいた。以降は Schedule に統一。実装名 `ActionProgram` / `--action` は v1 のまま）。中は `0x03/0x57/0x0A` で
  `0x2B`/`0x3D` のモーション名を参照する「振り付け」。
- **Base（ベースエフェクト）** = `ROM[n]\0\0.DAT`（先頭ブロック名 `syst`）。
  `List/Effect/*.csv` の先頭行 `0/0,syst` がこれ。**全エフェクトが共有する土台**
  （実測: 539,776 B / 1,105 チャンク / `0x05`=320 ジェネレータ / `0x20`=61 テクスチャ / `0x07`=132）。
  個別エフェクト DAT に無い名前は、このベースから解決する。

## 1. 目的・スコープ

### 目的
List から選んだ項目を**そのまま画面に表示**する。Effect なら再生、NPC ならモデル表示＋Schedule 再生。

**表示の規約（重要）**
- **起動時は何も表示しない**（背景のみ。座標軸・グリッド・既定モデル/エフェクトも出さない）。
- **表示のトリガーは List の項目の単一選択**（クリック。ダブルクリックも同義＝後方互換）。
  Schedule の選択は、既に表示中のモデルの Schedule を再生する操作（2026-11-09 変更。v1 はダブルクリック確定だった）。
- ベースエフェクト（`0/0`）は起動時に読むが**索引のみ**で、描画は一切しない。
- **画面描画は DirectX 11（D3D11）前提**: `D3D11CreateDeviceAndSwapChain`（Feature Level は **11_0 → 10_1 → 10_0** の順でフォールバック、`Gfx.cpp:115-119`）、
  `ID3D11Device` / `ID3D11DeviceContext` ＋ DXGI スワップチェーン。ビルドは **v143 / C++17 / Windows SDK 10.0** で
  `d3d11.lib; dxgi.lib; d3dcompiler.lib` をリンク（`EffectTest.vcxproj` の `PlatformToolset`=v143 / `LanguageStandard`=stdcpp17 / `WindowsTargetPlatformVersion`=10.0）。
  **D3D12 / Vulkan / OpenGL への移行は本設計の対象外**。
- **画面描画は秒間 60 コマ（60 fps / 1 コマ = 16.67 ms）を前提**とする。待ち時間・遅延は**コマ数**で表す
  （秒で表す場合は 60 fps 換算。例: 7 コマ = 116.7 ms）。エフェクトの時間基準（`delay / 60`）と同一の時間軸を使う。
  - 適用範囲: **UI・選択・デバウンスなどの待ちはコマ基準**で設計する。エフェクト/モーションの**進行は既存の実測 `dt`**
    （`App.cpp:1177`、0.1 s クランプ）を維持する（60 fps 環境では実質 1/60 秒ステップ。挙動変更は本設計の対象外）。

### スコープ内
- **起動時にベースエフェクト（`ROM\0\0.DAT`）を必ず先読み**（詳細は §4.6）
- List のグループ／項目／Schedule の 3 段選択 UI（3 段目は常時見える LISTBOX）
- NPC 項目 → モデル（メッシュ・ボーン・テクスチャ）表示
- Schedule 項目 → 対応モーションの再生（骨アニメーション）
- Effect 項目 → 既存の effect motion 再生（タイムライン + 2 段 VM）
- Effect と NPC の**同時再生**（P4、任意）

### スコープ外
- FBX / MQO / X 形式のエクスポート
- 4 面図保存・シャドウマップ等の ModelTest 既存高機能（壊さないが拡張しない）
- 0x1F（`f101.m`）ジオメトリの解読（別課題）

## 2. 現状の実測（判断材料）

| 項目 | EffectTest（現行） | ModelTest（参照実装） |
|---|---|---|
| 描画 | D3D11 自作ミニマム（`Gfx.cpp` 18KB、ビルボードのみ） | D3D11 フル（branch `DX11`／`Dx.cpp` 37KB、`Render.cpp` 18KB、スキニング/影/トゥーン） |
| モデル読込 | **なし** | `Model.cpp` 53KB / `Model_Mesh.cpp` 38KB / `Model_Anim.cpp` 29KB / `Model_Common.cpp` 30KB |
| Effect | タイムライン + VM 完備（`Effect.cpp` 10KB, `EffectVm.cpp` 9KB, 実データ一致検証済） | なし |
| List UI | パネル実装済（`ListDb` 7KB + `App.cpp` 内 UI、29 グループ / 最大 1,266 件、UTF-8/SJIS 自動判定） | 独自 UI（NPC/PC/装備/モーションのコンボ、`List/*.lst` 直読み） |
| FBX 依存 | なし | **`SaveFile.cpp`（91KB）のみ**（`SaveFile.cpp` 以外に `fbxsdk` 参照 0 件を実測） |

### モデル読み込みの実測フロー（ModelTest）
```
SetBody( ConvertStr2Dno2(name) )   // "1-131-6" → Dno
  → LoadNPC()                      // 0x2A/0x29/0x20 を DAT から解析
  → InitialTransform()
  → ConvertMesh()                  // スキニング用頂点生成
  → SetMotionName("atk0"); LoadNPCMotion()   // 0x2B/0x3D を適用
  → Rendering()
```
テクスチャは **同一 DAT 内の `0x20` チャンク**を走査して `CMaterial::SetTexture(SRV)` に渡す
（`Model_Common.cpp` 493–593 行、DXT1/DXT3/DXT5 + 8bit パレット ＝ EffectTest の `Tim.cpp` と同等）。

### モーション解決の実測（`CNPC::LoadNPCMotion` → `CModel::SetMotion3FromList`）
```cpp
// Model.cpp:1545
bool CNPC::LoadNPCMotion() {
    InitMotionData();
    GetFileNameFromDno(FileName, GetBody());   // 同じ NPC DAT
    LoadMotionFromFile(FileName);              // 同一 DAT の 0x2B を全部 m_motions へ
    SetMotion3FromList(0, GetMotionName());    // ★先頭 3 文字の前方一致
}
// Model.cpp:518  SetMotion3FromList → strncmp(MotionName, m_Name, 3) で一致した骨モーションを適用
```
- 動きの一覧は **同一 DAT の `0x2B`**。ModelTest の NPC モーション combo は
  その**先頭 3 文字**（`at0, at1, at2, stm, btl, cor, dbi, …`）。
- Schedule（`0x07`）の参照名は **先頭 3 文字一致**で解決。例：`atk4`→0x3D、`vatk`→0x07（入れ子 Schedule）。
- **一致しない参照はモーション無しとして無視**（`hwat` は同 DAT に存在せず、外部 DAT 探索はしない）。

### テクスチャの実測（`ROM\131\6.DAT`）
| チャンク | サイズ | 形式 | 判定 |
|---|---|---|---|
| `gob0` | 256×256 | **DXT3**（BC2） | EffectTest `Tim.cpp` でデコード可能（実装済み） |
| `hf_a` | 64×64 | **DXT3**（BC2） | 同上 |

→ **R1 は「解消」**：モデルのテクスチャは同一 DAT 内に存在し、既存コードで処理できる。

## 3. 設計案の比較

| 案 | 内容 | 工数 | リスク | 得られるもの |
|---|---|---|---|---|
| **D（推奨）** | **ModelTest を土台に EffectTest の機能を移植**（List パネル + Effect 再生） | 小〜中 | 低（モデル描画・モーションは完成済み） | List 選択でモデルが動き、Effect も重ねられる |
| C | EffectTest を土台にモデル描画を新規実装 | 大（解析コードの移植 100KB+） | 中（車輪の再発明、リグレッション） | 単一アプリで完結 |
| A | ModelTest のパイプライン（Dx/Render/Model 全部）を EffectTest に複製 | 中 | 中（`WinMain.cpp` 63KB の UI 依存を切り離す必要） | 単一アプリ、ただし実質 ModelTest の再構成 |
| B | ModelTest を DLL 化して EffectTest から呼ぶ | 中 | 高（`g_mView`/`Dx`/`Render` のグローバル越し設計変更） | 二重管理の回避 |

### 推奨: 案 D
**理由**: 表示したいもののうち「モデル＋モーション」は ModelTest で既に完成しており、
未完成なのは EffectTest 側の「List UI」と「Effect 再生」だけ。移植量は約 25KB（`ListDb` 7KB +
`Effect`/`EffectVm` 19KB + ビルボード描画 3KB）で、ModelTest の 150KB+ を動かすより圧倒的に小さい。

---

## 4. 推奨案（D）のアーキテクチャ

### 4.1 コンポーネント構成

```
┌────────────────────────── ModelTest.exe ──────────────────────────┐
│ WinMain.cpp（既存 UI）                                             │
│   ├─ 既存: 種族/装備/モーション/Playback                           │
│   └─ 追加: ListPanel（グループ／項目／Schedule の 3 段）  ★新規   │
│                                                                    │
│  選択ハンドラ（★新規: ListBridge.cpp）                             │
│   ├─ NPC 項目 → CModel::SetBody/LoadNPC/InitialTransform/          │
│   │              ConvertMesh          （既存 API を呼ぶだけ）       │
│   ├─ Schedule → ActionProgram（★新規）→ 0x2B 名解決 →             │
│   │              SetMotionName/LoadNPCMotion（既存 API）           │
│   └─ Effect 項目 → EffectView（★新規: EffectTest から移植）        │
│                                                                    │
│ 毎フレーム:                                                        │
│   CModel::DynamicTransform() → CModel::Rendering()  （既存）        │
│   EffectView::Advance(dt)    → EffectView::Render()（★新規）        │
└────────────────────────────────────────────────────────────────────┘
        依存: 既存 Dx.cpp（device/CB/shaders）と Render.cpp（カメラ）
```

### 4.2 データフロー

**NPC を選んだとき**
```
グループ combo → ListDb::Items(group)            … 1,266 件でも遅延ロードで O(1)
項目を単一選択 → ListDb::ResolvePath → "…\ROM\131\6.DAT"
              → CModel::SetBody(dno); LoadNPC(); InitialTransform(); ConvertMesh()
              → DAT の 0x07 名（39 個）を Schedule リストへ
Schedule 選択  → ActionProgram::Parse(dat, "atk0")   … 命令列 6 件
              → 参照名の**先頭 3 文字**で同一 DAT の 0x2B を前方一致解決
                 （"vatk" は 0x07 の入れ子 Schedule、"hwat" は一致無し＝無視）
              → CModel::SetMotionName(name); LoadNPCMotion(); SetTime(0)
```
**起動時（必須・毎回）**
```
WinMain 起動 → FFXI 検出 → List 読み込み（29 グループ）
             → EffectBase_Load("<FFXI>\ROM\0\0.DAT")        … syst = 共有ベース
                 ・0x05 320 個のジェネレータを名前索引（"hit1","eis1" …）
                 ・0x20  61 枚のテクスチャを SRV 化
                 ・0x07 132 個のタイムライン名を索引（入れ子 Schedule 用）
              → 描画は行わない（空のシーン：背景のみ・座標軸も非表示）
```
> 起動時は**表示ゼロ**。表示は **List の項目選択（ダブルクリック）** がトリガー。
> ベースの読み込みに失敗しても**起動は継続**し、個別 DAT のみで動作する（警告ログ）。

**Effect を選んだとき**
```
グループ combo → ListDb::Items(group)            … Effect カテゴリ
項目を単一選択 → ResolvePath → "…\ROM\11\17.DAT"
              → EffectView::Load(path)
                  EffectContext::Build()        … main タイムライン + 0x05 索引
                  BuildTextures()               … 0x20 → SRV（既存 Tim.cpp 相当）
毎フレーム     → EffectView::Advance(dt) → ビルボード 6 頂点 × N
```

### 4.3 UI 仕様（既存パネルへの追加）

既存の「種族・装備等」パネルに **List セクション**を 1 つ追加する（別ウィンドウは増やさない）。

| コントロール | ID | 内容 |
|---|---|---|
| グループ | `IDC_LIST_GROUP` | `NPC ROM1`〜`ROM9` / `Effect: White Magic` … |
| 項目リスト | `IDC_LIST_ITEMS` | `名前 (ID)` を列挙。**単一選択で確定**（ダブルクリックも同義） |
| Schedule | `IDC_LIST_SCHEDULE` | NPC のときだけ有効。`0x07` 名を**常時見える LISTBOX** に列挙し、件数をラベルに表示（v1 の「Action」） |
| Effect 同時再生 | `IDC_LIST_WITHEFFECT` | チェックで Effect を併走（P4） |

- 「表示切替」ラジオに **`List`** を 1 つ追加し、選択中は既存の種族/装備コンボを無効化する。
- 文字コードは既存の `ListDb`（UTF-8 / Shift-JIS 自動判定 → `wstring`）をそのまま使う。

### 4.4 ファイル構成（新規・変更）

| 種別 | パス | 責務 | 規模感 |
|---|---|---|---|
| 新規 | `ListDb.h/.cpp` | List 読み込み・ID→パス解決・文字コード変換 | 約 250 行（EffectTest から移植・依存なし） |
| 新規 | `ListPanel.h/.cpp` | グループ CB／項目 LB／Schedule LB の生成・イベント処理・選択結果の公開 | 約 250 行 |
| 新規 | `ActionProgram.h/.cpp` | NPC `0x07` の命令列解析、参照名→`0x2B/0x3D` 解決 | 約 200 行 |
| 新規 | `EffectView.h/.cpp` | EffectTest の `Effect.cpp`+`EffectVm.cpp` を移植（`Dx.h` の SRV に接続） | 約 700 行 |
| 新規 | `EffectBillboard.h/.cpp` | 加算合成ビルボード描画（ModelTest の device/context を利用） | 約 200 行 |
| 変更 | `WinMain.cpp` | List セクションの生成、選択ハンドラ、毎フレーム呼び出し | +約 150 行 |
| 変更 | `resource.h` / `resource.rc` | コントロール ID とレイアウト | +約 40 行 |
| 変更 | `ModelTest.vcxproj` | 上記ファイル追加 | 数行 |
| 変更なし | `Model*.cpp` / `Render.cpp` / `Dx.cpp` | そのまま利用（**改変しない**） | — |

### 4.5 主要 API（タスク間の契約）

```cpp
// ---- ListDb ----
bool  ListDb::Load(const char* listDir);                       // "<exe>\\List"
size_t ListDb::GroupCount() const;
const ListGroup& ListDb::Group(size_t i) const;                // wlabel/label
const std::vector<ListItem>& ListDb::Items(size_t gi);         // 遅延ロード
static bool ListDb::ResolvePath(const char* ffxiBase, const ListGroup& g,
                                const ListItem& it, char* out, size_t n);
// ListItem { std::string id, name; std::wstring wname; int rom; }

// ---- ListPanel（Win32 子ウィンドウ群）----
void ListPanel_Create(HWND parent, const char* listDir, const char* ffxiBase);
bool ListPanel_HandleCommand(HWND parent, WPARAM wp);           // WM_COMMAND を転送
void ListPanel_SetActions(const std::vector<std::string>& names); // NPC 選択後
struct ListSelection { int group=-1, item=-1, action=-1; bool isEffect=false; };
bool ListPanel_GetSelection(ListSelection* out);

// ---- ActionProgram（NPC 0x07）----
struct ActionInst { uint8_t op, len; uint16_t delay, param; char name[5]; uint32_t offset; };
bool ActionProgram_Parse(const DatFile& dat, const char* action4,
                         std::vector<ActionInst>* out, float* durationSec);
bool ActionProgram_CollectMoveNames(const DatFile& dat, const char* action4,
                                    std::vector<std::string>* names);  // "hwat","atk1" …

// ---- EffectView（EffectTest から移植）----
bool EffectView_Load(const char* datPath);
void EffectView_Advance(float dt);
void EffectView_Render(ID3D11DeviceContext* ctx);   // 加算合成ビルボード
void EffectView_Reset();

// ---- EffectBase（起動時に必ず読む共有ベース = ROM\0\0.DAT）----
bool EffectBase_Load(ID3D11Device* dev, const char* baseDatPath);  // "…\ROM\0\0.DAT"
void EffectBase_Unload();
bool EffectBase_IsLoaded();
const DatFile*       EffectBase_Dat();                              // 名前解決用
const GeneratorBlock* EffectBase_FindGenerator(const char* name4);   // 0x05
const DatChunk*      EffectBase_FindTimeline(const char* name4);    // 0x07
ID3D11ShaderResourceView* EffectBase_FindTexture(const char* name4); // 0x20
const char*          EffectBase_Path();                             // ログ用
```

> 関数名・構造体名は v1 の実装名を維持する（`ActionProgram` / `ListSelection.action` / `ListPanel_SetActions`）。
> UI 上の呼称は **Schedule**（§0・§4.7）。

### 4.6 ベースエフェクト（`0/0` = `syst`）の起動時ロード

**なぜ必要か**: `List/Effect/*.csv` の先頭行 `0/0,syst` が示すとおり、`ROM\0\0.DAT` は
全エフェクトが共有する土台（共通ジェネレータ・共通テクスチャ・共通サブエフェクト）である。
個別エフェクト DAT（例 `11/17` Fire）だけでは名前が解決できない参照があるため、
**起動時にベースを索引しておき、名前解決のフォールバック先**にする。

**実測（`ROM\0\0.DAT`）**

| 項目 | 値 |
|---|---|
| サイズ | 539,776 B |
| チャンク数 | **1,105** |
| `0x05` ジェネレータ | **320** |
| `0x20` テクスチャ | **61**（例 `plig`, `spec`, `test`） |
| `0x07` タイムライン | 132（例 `effe`） |
| `0x1F` / `0x19` / `0x21` / `0x3D` | 45 / 295 / 16 / 98 |
| 先頭ブロック | `syst`（0x01）→ `coll`(0x1F) → テクスチャ 3 枚 → `ncol`(0x31) → `effe`(0x01) |

**名前解決の順序（Effect）**
```
1) 選択中のエフェクト DAT（例 ROM\11\17.DAT）の 0x05 / 0x07 / 0x20
2) 見つからなければ EffectBase（ROM\0\0.DAT）の 0x05 / 0x07 / 0x20
3) どちらにも無ければ「未解決」としてスキップ（ログに記録）
```
> NPC 側（§4.2 の Schedule→モーション）は従来どおり**同一 DAT の `0x2B` を 3 文字一致**で解決し、
> ベースは**参照しない**（モデルのモーションはモデル DAT 内で完結するため）。

**ROM ごとの `0/0` について**
`Effect7.csv` の `0/0,Flowerpot` のように、**ROM 番号ごとに別の `0\0.DAT`** が存在する。
- 既定は `ROM\0\0.DAT`（ROM1）をベースとして読み込む
- 選択カテゴリの ROM が 1 以外（`Effect2`〜`Effect9`）の場合は、**その ROM の `ROM<n>\0\0.DAT` を
  追加ベース**として読み込む（`EffectBase_Load` を ROM 分だけ追加可能にする）
- メモリ: 1 ベース最大 0.5 MB（チャンク索引＋テクスチャ）なので 9 個読んでも実用範囲

**失敗時の扱い**
- ベースが存在しない/壊れている → **警告ログのみで継続**（個別 DAT 単独で動作）
- ベースのテクスチャ生成に失敗した枚はスキップし、白テクスチャで代替

**検証（P0 に含める）**
```
ModelTest.exe --base
期待: [base] path=…\ROM\0\0.DAT chunks=1105 gen=320 tex=61 timeline=132 load_ms=xx
```

**実測（EffectTest に先行実装して確認済み）**
```
[base] path=…\ROM\0\0.DAT chunks=1105 gen=320 tex=61 timeline=132 load_ms=0   ← --base（索引のみ / D3D なし）
[base] path=…\ROM\0\0.DAT chunks=1105 gen=320 tex=61 timeline=132 load_ms=3   ← 起動時（DXT3 61 枚の SRV 生成込み）
```
→ **起動時ロードのコストは 3 ms（≈ 0.18 コマ）**。毎回読んでも体感影響なし（実測値であり、設計上の待ちではない）。

### 4.7 Schedule リスト設計（v2 / 2026-11-09）

> この節が **UI と選択フローの正本**。§1・§4.1〜4.3・§5 の該当箇所は本節に合わせて更新済み。

#### 4.7.1 決定事項

| # | 決定 | 根拠 |
|---|---|---|
| D1 | **モデルの単一選択（クリック / キー移動）で即時**に DAT を読み、Schedule 一覧を更新する。ダブルクリックは同義（後方互換） | AltanaView と同じ操作感。現行はダブルクリックするまで 3 段目が空だった |
| D2 | 3 段目は COMBOBOX ではなく **LISTBOX**（常時見える一覧）＋件数ラベル | 一覧性。`Schedule (39)` のように件数を併記 |
| D3 | 用語は **Schedule** に統一（= `0x07`。AltanaView の `[NPC] Schedule` と同義）。v1 の「Action」は別名 | `AltanaView.ini` の `Schedule=` / 実測 `ROM\131\6.DAT` の `0x07`=39 件 |
| D4 | **7 コマ（60 fps で 116.7 ms）静止後に確定**＋確定 index ガード（待ちはコマ基準） | 1,266 件リスト（R4）でのキー連打耐性 |
| D5 | Schedule は**入れ替え**（蓄積しない＝常に選択中の 1 モデル分） | `0x07` はモデル DAT 内の資源。混在させると解決先が曖昧になる |
| D6 | Schedule は**行の選択で即再生**（現行の「選択＝再生」挙動を踏襲）。`R` リセット / `Space` 一時停止 | 既存操作との連続性（`App.cpp:469` / `:453`） |

#### 4.7.2 コントロールと レイアウト

> 以下は**現行 EffectTest（`App.cpp`）の実装**に対する指定。案 D（ModelTest 土台）側の §4.3 では
> `IDC_LIST_*` の名称になるが、構造（グループ CB／項目 LB／Schedule LB）は同一。

| コントロール | ID | 型 | 内容 | 有効条件 |
|---|---|---|---|---|
| グループ | `IDC_GROUP` = 1001 | `COMBOBOX`（`CBS_DROPDOWNLIST`） | 29 グループ | 常時 |
| 項目 | `IDC_ITEMS` = 1002 | `LISTBOX`（`LBS_NOTIFY`） | `名前 (ID)`（最大 1,266 件） | 常時 |
| Schedule ラベル | `IDC_SCHEDULE_LABEL` = 1004 | `STATIC` | `Schedule (39)` / 未確定は `Schedule (-)` | 常時 |
| **Schedule** | `IDC_SCHEDULE` = 1003 | `LISTBOX`（`LBS_NOTIFY｜WS_VSCROLL｜LBS_NOINTEGRALHEIGHT`） | 選択モデルの `0x07` 名 | NPC 確定時のみ（Effect 確定・未選択は `EnableWindow(FALSE)`） |

- `IDC_ACTION`（1003）の**値は据え置き**、名前のみ `IDC_SCHEDULE` に変更（差分を小さく保つ）。
- 内部名: `g_hAction`→`g_hSchedule`、`g_ActionNames`→`g_ScheduleNames`、`PlaySelectedAction`→`PlaySelectedSchedule`、ログ接頭辞 `[action]`→`[schedule]`。

```
w = クライアント幅 - 16、bottom = クライアント高
g_hGroup         : x=8, y=8,             w, h=200        … ドロップダウン展開高
g_hItems         : x=8, y=32,            w, h=bottom-194 … 残余はすべて項目に与える
g_hScheduleLabel : x=8, y=bottom-158,    w, h=18
g_hSchedule      : x=8, y=bottom-136,    w, h=128         … 6〜7 行が見える高さ
```

- `bottom < 320` では Schedule を 96、項目の最小高を 64 として負値を出さない。

#### 4.7.3 イベントとデータフロー

```
WM_COMMAND / IDC_ITEMS / LBN_SELCHANGE または LBN_DBLCLK
    g_PendingItem   = LB_GETCURSEL
    g_PendingFrames = 0                    … 選択が来るたびにカウンタを戻す

描画ループ（毎フレーム = 60 fps 前提）          // TickPendingCommit()
    g_PendingItem < 0                 → return
    ++g_PendingFrames < 7             → return      // 7 コマ（116.7 ms）静止したら確定
    CommitSelection(g_PendingItem); g_PendingItem = -1

CommitSelection(index):                        // 唯一の確定経路
    index < 0 || g_CommitBusy            → return
    同一グループ && index == 確定済み index → return      // キー移動・ダブルクリックの 2 回目を吸収
    ResolvePath 失敗 → ClearSelection("resolve failed", id)
    LoadDatFile 失敗 → ClearSelection("load failed", path)
    Effect グループ:
        Schedule を空・無効化、ラベル "Schedule (-)"、g_Effect.Reset()、g_Paused = false
    NPC グループ:
        g_ScheduleNames = g_Dat の 0x07 名（チャンク順）
        Schedule へ LB_ADDSTRING → 先頭を LB_SETCURSEL（自動選択のみ／再生しない）
        有効化、ラベル "Schedule (<件数>)"
    UpdateTitle()（EffectTest はタイトル/ログが表示更新点。モデル描画は P1）

WM_COMMAND / IDC_SCHEDULE / LBN_SELCHANGE または LBN_DBLCLK
    PlaySelectedSchedule(LB_GETCURSEL)          // BuildFromChunk 失敗はログのみ
```

- **自動選択で再生しない理由**: `LB_SETCURSEL` は `LBN_SELCHANGE` を送出しないため、先頭の自動選択で勝手に再生が始まることはない。利用者が明示的に選んだ行だけ再生する（D6）。
- **後始末 `ClearSelection(reason, detail)`**: `g_Effect.ReleaseTextures()`（`Effect.cpp:193`）→ `g_Effect.Reset()`（`:203`）→ `g_ActionCtx.Reset()` → Schedule 空・無効 → `g_Dat.Unload()`（`Dat.h:34`）→ `g_DatPath` 空 → 確定 index を -1 → `UpdateTitle()`（案内文へ）→ ログ `[schedule] clear: <reason> <detail>`。

#### 4.7.4 状態モデル

| 状態 | `g_DatPath` | Schedule リスト | 画面 |
|---|---|---|---|
| 未選択（起動直後） | 空 | 空・無効 | 背景のみ（座標軸は既定 OFF） |
| Effect 確定 | `…\ROM\11\17.DAT` | 空・無効 | エフェクト再生 |
| NPC 確定 | `…\ROM\131\6.DAT` | 39 件・有効／先頭選択 | エフェクト無し（モデル描画は P1） |
| 失敗 | 空 | 空・無効 | 背景のみ＋タイトルは案内文 |

#### 4.7.5 失敗・空の扱い

| 事象 | 挙動 |
|---|---|
| `ResolvePath` 失敗（ROM 未インストール等） | `ClearSelection("resolve failed", id)`。**直前の表示を残さない**（§1 の起動時規約と整合） |
| DAT 読み込み失敗（破損・0 バイト） | `ClearSelection("load failed", path)`。`DatFile::Load` は先頭で `Unload()` する（`Dat.cpp:8`）ため古い DAT は残らない |
| `0x07` が 0 件 | 正常系。ラベル `Schedule (0)`、リスト有効・空、ログ `[schedule] count=0` |
| Schedule がタイムラインを持たない | ログのみ。再生位置は変えない |
| ベース `ROM\0\0.DAT` 失敗 | 従来どおり警告のみで起動継続（R7） |

#### 4.7.6 性能・再入防止

| 対策 | 内容 |
|---|---|
| デバウンス | **7 コマ**（60 fps で 116.7 ms）静止後に確定。選択が来るたびにカウンタを 0 に戻す（↑↓ 長押し・連続クリックで DAT 読みが発火しない） |
| 確定 index ガード | 同一グループ・同一 index は no-op（ダブルクリックの 2 回目を吸収） |
| 再入フラグ | `g_CommitBusy` で `CommitSelection` の多重実行を抑止 |
| 保留の破棄 | グループ変更（`FillItems`）で `g_PendingItem = -1` / `g_PendingFrames = 0`（確定待ちを取り消す） |

**目標値**: 単一選択→Schedule 反映 **3 コマ以内（50 ms 以下）**（ROM1 最大 DAT、`load_ms` をログ出力）／1,266 件で ↑↓ 長押し **120 コマ（2 秒）**でも **60 fps 維持**（フレーム落ち 5% 未満）。

> **本設計は秒間 60 コマを前提**とする。待ちは時間ではなく**コマ数**で持ち、実装も描画ループのフレームカウンタで消化する（`SetTimer` / `WM_TIMER` は使わない＝コマ基準を保証するため）。
> 60 fps の根拠: エフェクトの時間基準が `delay / 60.0f`（`Effect.cpp:232`）、描画は **D3D11 + vsync**（`Gfx.cpp:102-113` の `DXGI_SWAP_CHAIN_DESC`＝`Windowed=TRUE` / `BufferCount=1` / `DXGI_SWAP_EFFECT_DISCARD`、`:310` の `Present(1,0)`）＝60 Hz のモニタで 60 fps。
> 保証対象は **60 fps を満たす環境**。それを下回る環境（30 Hz 等）は**本設計の対象外**とする（7 コマ = 233 ms となり待ちが伸びる。再生速度・位置は実測 `dt` のため影響を受けない）。

#### 4.7.7 現行 EffectTest への影響（`App.cpp` のみ）

| 箇所 | 現状 | 変更 |
|---|---|---|
| `enum`（`:80`） | `IDC_ACTION` | `IDC_SCHEDULE`（値 1003 維持）、`IDC_SCHEDULE_LABEL=1004`、`kCommitDelayFrames = 7`（定数） |
| グローバル（`:64-75`） | `g_hAction` / `g_ActionNames` | `g_hSchedule` / `g_hScheduleLabel` / `g_ScheduleNames` / `g_PendingItem` / `g_PendingFrames` / `g_CommittedItem` / `g_CommittedGroup` / `g_CommitBusy` |
| `BuildPanelControls`（`:537`） | COMBOBOX 1 個 | LISTBOX＋STATIC に置換、初期は `EnableWindow(FALSE)` |
| `LayoutPanel`（`:524`） | 3 要素 | 上記の式へ |
| `FillItems`（`:575`） | `CB_RESETCONTENT` | 確定待ちの破棄（`g_PendingItem = -1`）、Schedule 空・無効、確定 index リセット |
| `LoadListSelection`（`:599`） | 確定処理 | `CommitSelection` に改称（§4.7.3 の分岐） |
| `PlaySelectedAction`（`:648`） | `g_ActionNames` | `PlaySelectedSchedule` に改称（中身は同一） |
| `PanelProc`（`:686-704`） | `LBN_DBLCLK` のみ | `LBN_SELCHANGE` を追加（選択を pending に記録）、`IDC_SCHEDULE` の `LBN_SELCHANGE`/`LBN_DBLCLK`。**`WM_TIMER` は使わない** |
| 描画ループ（`:1166-1182`） | `g_Effect.Advance(g_Dt)` のみ | 毎フレーム `TickPendingCommit()` を呼び、7 コマ静止で確定（`kCommitDelayFrames`） |
| 新規ヘルパ | — | `ScheduleClear()` / `ClearSelection()` / `UpdateScheduleLabel()`（計 30 行程度） |

- 変更しない: `Dat.*` / `Effect.*` / `EffectVm.*` / `ListDb.*` / `Tim.*` / `Gfx.*` / `EffectBase.*`
- 検証モードの引数と既存出力書式は不変（`--action` の出力はそのまま回帰確認に使う）。
- 実装時は `ReadMe.md` の操作説明（「ダブルクリックで確定」→「単一選択で確定」、「Action」→「Schedule」）も更新する。

#### 4.7.8 検証

| 層 | 手法 | ゴールデン |
|---|---|---|
| 回帰（List） | `--list` | 29 グループ / NPC ROM1 = 1,266 件 |
| Schedule 列挙 | 新規 `--schedule <dat>`（1 行 1 件） | `ROM\131\6.DAT` の `0x07` = **39 件** |
| 回帰（Effect） | `--shot "…\ROM\11\17.DAT" out.png` | timeline=18 / spawn=8 / 3.3333 s / 非背景 > 6,000 |
| UI 手動 | `NPC ROM1` → `ヒューム♀NPC (3-11)` を**シングルクリック** | ラベル `Schedule (n)`、リストに `0x07` 名が並ぶ |
| UI 手動（失敗系） | 存在しない ROM / 壊れた DAT | 背景のみに戻り、ログに `clear:` |
| 性能 | ↑↓ 長押し **120 コマ（2 秒）**・連続クリック（60 fps 維持を計測） | §4.7.6 の目標値 |



---

## 5. フェーズ計画（各フェーズが単独で動作確認できる単位）

> 実装は **フェーズ順**。各フェーズの終わりに「受け入れ基準」と「検証コマンド」を満たすこと。

### P0: List 読み込み＋**ベースエフェクト起動ロード**＋選択 UI（0.7 日）
**成果物**: `ListDb.h/.cpp`, `ListPanel.h/.cpp`, **`EffectBase.h/.cpp`**, `WinMain.cpp` への組み込み,
`--list` / `--base` 検証モード
**受け入れ基準**
- グループ 29 件（NPC ROM1〜9 + Effect 20 カテゴリ）が列挙される
- `NPC ROM1` で 1,266 件、`Effect: Black Magic` で 166 件が入る
- 日本語が正しく表示される（UTF-8 / Shift-JIS 混在でも化けない）
- 項目選択で DAT のフルパスが解決される（`1-131-6` → `ROM\131\6.DAT`、`11/17` → `ROM\11\17.DAT`）
- **起動直後は画面に何も描画されない**（背景のみ・座標軸なし・既定モデル/エフェクトなし）
- **起動直後に `ROM\0\0.DAT`（syst）が読み込まれ、1,105 チャンク / gen 320 / tex 61 / timeline 132 が索引される**（描画はしない）
- ベースが無い環境でも**警告のみで起動継続**する
**検証**
```
ModelTest.exe --list
期待: [list] groups=29 / items=1266 / オーク神 id=3-2 -> …\ROM\3\2.DAT

ModelTest.exe --base
期待: [base] path=…\ROM\0\0.DAT chunks=1105 gen=320 tex=61 timeline=132 load_ms=<数値>
```

### P1: NPC モデルを表示（0.5 日）
**成果物**: `ListBridge.cpp`（選択→モデル読み込み）
**受け入れ基準**
- `NPC ROM1` → `裏ゴブリン` を**単一選択**するとモデルが画面に出る
- 同じ操作の直後に **Schedule リストが埋まる**（`ROM\131\6.DAT` = 39 件、ラベル `Schedule (39)`）
- 既存の PC/NPC コンボによる表示が**従来どおり動く**（回帰なし）
- テクスチャが貼られている（白一色でない）
**検証**
```
ModelTest.exe --shot "ROM\131\6.DAT" out_npc.png
期待: 非背景ピクセル > 0（4px 間引きで数千）
```

### P2: Schedule 選択 → モーション再生（1 日）
**成果物**: `ActionProgram.h/.cpp`（`0x07` 解析・参照名解決）
**受け入れ基準**
- Schedule リストに `0x07` 名が 39 件並ぶ（`ntob, pary, atk0, cast, vatk, dead …`）。**モデルを単一選択しただけで埋まる**
- `atk0` 選択で命令列 6 件がログに出て、参照名 `hwat`/`vatk` が**先頭 3 文字一致**で判定される
  （`vatk`→0x07 入れ子、`hwat`→一致無し＝警告ログのみ）
- モデルが実際に動く（`MaxMotionTime() > 0`、再生で骨が変化）
- 併せて **0x2B の 3 文字一覧**（`at0,at1,at2,btl,cor,…`）も一覧に出し、Schedule と独立に選べるようにする（ModelTest 互換）
**検証**
```
ModelTest.exe --action "ROM\131\6.DAT" atk0
期待: insts=6 / op=0x03 name=hwat / op=0x57 name=vatk
```

### P3: Effect 再生の移植（1〜1.5 日）
**成果物**: `EffectView.h/.cpp`, `EffectBillboard.h/.cpp`（EffectTest から移植）
**受け入れ基準**
- `Effect: Black Magic` → `Fire` で、EffectTest と**同一の数値**（18 命令 / 8 生成 / **3.3333 s = 200 コマ**）
- 画面にエフェクトが出る（加算合成・テクスチャ付き）
**検証**
```
ModelTest.exe --effect "ROM\11\17.DAT" --shot out_fire.png
期待: timeline=18 / spawn=8 / duration=3.3333 / 非背景 > 6000 サンプル
```

### P4: 同時再生（1 日 / 任意）
**受け入れ基準**
- NPC（P1/P2）と Effect（P3）が**同じフレームに共存**して描画される
- Effect はモデルの背後/前面どちらでも破綻しない（Z 書き込みなし・加算合成）
- **60 fps（16.67 ms/コマ）を維持**（モデル 1 体 + エフェクト 8 インスタンス）
- （可能なら）対象ボーン位置へエフェクトを追従させる

### P5: 仕上げ（0.5 日）
- 既存機能の回帰（PC 表示・モーション・4 面図・SAVEMQO/SAVEX/SAVEFBX）
- ReadMe / 設計書更新、スクリーンショット取得

**合計: 約 5〜5.5 日**（P0 にベースエフェクトの起動ロードを含む）

---

## 6. テスト方針

| 層 | 手法 | 判定 |
|---|---|---|
| List 解析 | ヘッドレス `--list` | グループ数・件数・解決パスを**ゴールデン値**と比較 |
| Schedule 解析 | ヘッドレス `--action` | 命令数・`op`/`name` 列をゴールデン値と比較 |
| Schedule 列挙 | ヘッドレス `--schedule <dat>`（新規） | `0x07` 名の一覧がゴールデン（`ROM\131\6.DAT` = 39 件）と一致 |
| Effect | ヘッドレス `--effect` | 18 命令 / 8 生成 / 3.3333 s（EffectTest と一致） |
| 描画 | `--shot` で PNG → 画素検査 | 非背景サンプル数・最大 RGB 和 |
| 回帰 | 既存の PC/NPC 表示、4 面図、保存系 | 例外・クラッシュなし、出力ファイル生成 |

> ゴールデン値は本設計書 §5 の受け入れ基準に記載した実測値を使用する（＝仕様が先に存在する）。

---

## 7. リスク・未確定事項

| # | リスク | 影響 | 対応 |
|---|---|---|---|
| ~~R1~~ | ~~モデルのテクスチャが DAT 内 `0x20` で解決できない~~ | — | **解消（実測）**: `ROM\131\6.DAT` の `gob0`/`hf_a` は同一 DAT 内の **DXT3**。既存 `Tim.cpp`（BC2 対応）でデコード可能 |
| ~~R2~~ | ~~`0x3D` の意味が未確定~~ | — | **解消（実測）**: `CModel::SetMotion3FromList` は **先頭 3 文字前方一致**で `0x2B` を解決。`0x3D` は参照先の変種（`atk1..4`）。一致しない参照（`hwat`）は**無視**する仕様 |
| R3 | Schedule の未解読 op（`0x18`, `0x50`, `0x24`, `0x32`） | 一部の振り付けが再現されない | 命令長で読み飛ばし、ログに未対応 op を出す（数値は失わない） |
| R4 | 1,266 件のリスト投入が重い | UI が固まる | `WM_SETREDRAW` 抑制 + 遅延ロード（既に実装済みの方式を踏襲） |
| R5 | `EffectView` の SRV 所有権（ModelTest 側と二重解放） | クラッシュ | SRV は `EffectView` が所有し、`CMaterial` へは**借用**で渡す設計にする |
| R6 | モデル DAT ごとに `0x07` の開始オフセットが異なる | Schedule 解析失敗 | EffectTest で実装済みの**自動判定**（`main`=+0x80 / NPC=+0x50）を移植 |
| R7 | **ベース `ROM\0\0.DAT` のロード失敗**（未インストール ROM / 破損） | 共有参照が解決できず一部エフェクトが欠ける | 警告ログのみで**起動継続**。個別 DAT 単独でも動作する設計にする |
| R8 | ROM 別 `0\0.DAT`（Effect7 の `0/0,Flowerpot` 等）の取り違え | 誤ったベースで解決 | カテゴリの ROM（`index.csv` 3 列目）に応じて `ROM<n>\0\0.DAT` を選ぶ。ROM1 を既定 |

---

## 8. 意思決定が必要な点（レビュー依頼）

> **2026-11-09 更新（決定済み）**
> - 表示トリガーは **項目の単一選択**（ダブルクリックは同義）。
> - 3 段目は **Schedule の LISTBOX**（常時表示・件数ラベル付き）。蓄積はしない。
> - 詳細設計: 本書 **§4.7**（UI と選択フローの正本）

1. **土台アプリ**: 案 D（ModelTest を拡張）で進めてよいか？
   - 代替: 案 C（EffectTest にモデル描画を新規実装＝工数 2〜3 倍、リスク中）
2. **初期リリースに P4（同時再生）を含めるか**、まずは P1〜P3 で「List から選んで表示」を完成させるか
3. **Schedule → Effect の紐付け**（FFXI のデータに直接の対応表が無い）
   - (a) 紐付けしない（利用者が別々に選ぶ）
   - (b) 名前一致で自動（`Fire` と `casf` 等は一致しないため実質不可）
   - (c) 手動マップ CSV（`List/ActionEffect.csv` を新設し、`1-131-6,atk0,11/17` のように書く）
   - 推奨: **(a) で開始し、必要になったら (c)**


