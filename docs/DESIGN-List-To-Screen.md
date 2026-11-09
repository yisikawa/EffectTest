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
- **エフェクト資源の限定**: タイムラインが参照する generator と、その `+0xAC` が指す texture のみ構築（§4.8）
- Effect と NPC の**同時再生**（P4、任意）

### スコープ外
- FBX / MQO / X 形式のエクスポート
- 4 面図保存・シャドウマップ等の ModelTest 既存高機能（壊さないが拡張しない）
- 0x1F（`f101.m`）ジオメトリの解読（別課題）
- 設定ファイル（`EffectTest.ini`）による状態永続化（**別課題**。検証用 CLI とは分離して扱う）

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
                 ・0x20  61 枚は**索引のみ**（SRV 化は初回参照時。§4.8.4）
                 ・0x07 132 個のタイムライン名を索引（入れ子 Schedule 用）
              → 描画は行わない（空のシーン：背景のみ・座標軸も非表示）
```
> 起動時は**表示ゼロ**。表示は **List の項目の単一選択**がトリガー（§1・§4.7）。
> ベースの読み込みに失敗しても**起動は継続**し、個別 DAT のみで動作する（警告ログ）。

**Effect を選んだとき**
```
グループ combo → ListDb::Items(group)            … Effect カテゴリ
項目を単一選択 → ResolvePath → "…\ROM\11\17.DAT"
              → EffectView::Load(path)
                  EffectContext::Build()        … main タイムライン + 0x05 索引（開始オフセットは自動判定 §4.8.2）
                  BuildTextures()               … **必要分のみ** 0x20 → SRV（generator +0xAC から算出 §4.8.3）
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

### 4.6 ベースエフェクト（`0/0` = `syst`）の起動時索引（SRV は遅延）

**なぜ必要か**: `List/Effect/*.csv` の先頭行 `0/0,syst` が示すとおり、`ROM\0\0.DAT` は
全エフェクトが共有する土台（共通ジェネレータ・共通テクスチャ・共通サブエフェクト）である。
個別エフェクト DAT（例 `11/17` Fire）だけでは名前が解決できない参照があるため、
**起動時にベースを索引しておき、名前解決のフォールバック先**にする。

> **2026-11-09 の解析による変更**: 320 generator / 61 texture を**全部構築する必要はない**。
> 起動時は**索引のみ**（名前→チャンク）とし、**テクスチャ SRV は初回参照時に生成**する（§4.8 / `docs/ANALYSIS-Effect-Resource-Limiting.md`）。

**実測（`ROM\0\0.DAT`）**

| 項目 | 値 |
|---|---|
| サイズ | 539,776 B |
| チャンク数 | **1,105** |
| `0x05` ジェネレータ | **320** |
| `0x20` テクスチャ | **61**（例 `plig`, `spec`, `test`, `hit1`〜`hit9`, `eis1`〜`eis4`） |
| `0x05` の `+0xAC` リンク名 | 異なり **91 種**（うち **45 種が上記 `0x20` 名**、残りは `0x1F`/`0x21`/未解決） |
| `0x07` タイムライン | 132（例 `effe`） |
| `0x1F` / `0x19` / `0x21` / `0x3D` | 45 / 295 / 16 / 98 |
| 先頭ブロック | `syst`（0x01）→ `coll`(0x1F) → テクスチャ 3 枚 → `ncol`(0x31) → `effe`(0x01) |

**名前解決の順序（Effect）**
```
1) 選択中のエフェクト DAT（例 ROM\11\17.DAT）の 0x05 / 0x07 / 0x20
2) 見つからなければ EffectBase（ROM\0\0.DAT）の 0x05 / 0x07 / 0x20
3) どちらにも無ければ「未解決」としてスキップ（ログに記録）
   ※ 解決は必ず**型を指定**して行う（同名が 0x05 と 0x20 に併存: 例 base の hit1）
   ※ テクスチャは generator の **+0xAC リンク名**から引く（§4.8）
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
[base] path=…\ROM\0\0.DAT chunks=1105 gen=320 tex=61 timeline=132 load_ms=3   ← 従来方式: 起動時に 61 枚の SRV 生成込み
```
→ 索引のみなら **load_ms=0**。SRV を作る従来方式でも 3 ms（≈ 0.18 コマ）だが、§4.8 の方針で
**起動時は索引のみ・SRV は遅延生成**とする（実測値であり、設計上の待ちではない）。

### 4.7 Schedule リスト設計（v2 / 2026-11-09）

> この節が **UI と選択フローの正本**。§1・§4.1〜4.3・§5 の該当箇所は本節に合わせて更新済み。

#### 4.7.1 決定事項

| # | 決定 | 根拠 |
|---|---|---|
| D1 | **項目（モデル / エフェクト）の単一選択（クリック / キー移動）で即時**に DAT を読み、Schedule 一覧を更新する。ダブルクリックは同義（後方互換） | AltanaView と同じ操作感。現行はダブルクリックするまで 3 段目が空だった |
| D2 | 3 段目は COMBOBOX ではなく **LISTBOX**（常時見える一覧）＋件数ラベル | 一覧性。`Schedule (39)` のように件数を併記 |
| D3 | 用語は **Schedule** に統一（= `0x07`。AltanaView の `[NPC] Schedule` と同義）。v1 の「Action」は別名 | `AltanaView.ini` の `Schedule=` / 実測 `ROM\131\6.DAT` の `0x07`=39 件 |
| D4 | **7 コマ（60 fps で 116.7 ms）静止後に確定**＋確定 index ガード（待ちはコマ基準） | 1,266 件リスト（R4）でのキー連打耐性 |
| D5 | Schedule は**入れ替え**（蓄積しない＝常に選択中の 1 項目分） | `0x07` は DAT 内の資源。混在させると解決先が曖昧になる |
| D6 | Schedule は**行の選択で即再生**（現行の「選択＝再生」挙動を踏襲）。`R` リセット / `Space` 一時停止 | 既存操作との連続性（`App.cpp:469` / `:453`） |

#### 4.7.2 コントロールと レイアウト

> 以下は**現行 EffectTest（`App.cpp`）の実装**に対する指定。案 D（ModelTest 土台）側の §4.3 では
> `IDC_LIST_*` の名称になるが、構造（グループ CB／項目 LB／Schedule LB）は同一。

| コントロール | ID | 型 | 内容 | 有効条件 |
|---|---|---|---|---|
| グループ | `IDC_GROUP` = 1001 | `COMBOBOX`（`CBS_DROPDOWNLIST`） | 29 グループ | 常時 |
| 項目 | `IDC_ITEMS` = 1002 | `LISTBOX`（`LBS_NOTIFY`） | `名前 (ID)`（最大 1,266 件） | 常時 |
| Schedule ラベル | `IDC_SCHEDULE_LABEL` = 1004 | `STATIC` | `Schedule (39)` / 未確定は `Schedule (-)` | 常時 |
| **Schedule** | `IDC_SCHEDULE` = 1003 | `LISTBOX`（`LBS_NOTIFY｜WS_VSCROLL｜LBS_NOINTEGRALHEIGHT`） | 選択項目の `0x07` 名（**Effect / NPC 共通**。全件） | 項目確定時のみ（未選択は `EnableWindow(FALSE)`） |

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
    共通（Effect / NPC）:
        g_ScheduleNames = g_Dat の 0x07 名（チャンク順。**全件**）
        Schedule へ LB_ADDSTRING → **`main` を既定選択**（無ければ先頭）。自動選択のみ／再生しない
        有効化、ラベル "Schedule (<件数>)"
    Effect グループ:
        `LoadDatFile` が既定 Schedule（`main`、無ければ先頭の 0x07）を構築済み → **同一なら再構築しない**
        既定と異なる Schedule を選んだときだけ `BuildFromChunk`／`g_Paused = false`
    NPC グループ:
        モデル表示（P1）と Schedule 一覧の提示のみ（**自動では再生しない**。行を選ぶと `g_ActionCtx` で再生）
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
| Effect 確定 | `…\ROM\11\17.DAT` | **n 件・有効／`main` 選択**（例 1 件） | 既定 = `main` を再生。選択で他の `0x07` に切替 |
| NPC 確定 | `…\ROM\131\6.DAT` | 39 件・有効／`main` または先頭を選択 | エフェクト無し（モデル描画は P1） |
| 失敗 | 空 | 空・無効 | 背景のみ＋タイトルは案内文 |

#### 4.7.5 失敗・空の扱い

| 事象 | 挙動 |
|---|---|
| `ResolvePath` 失敗（ROM 未インストール等） | `ClearSelection("resolve failed", id)`。**直前の表示を残さない**（§1 の起動時規約と整合） |
| DAT 読み込み失敗（破損・0 バイト） | `ClearSelection("load failed", path)`。`DatFile::Load` は先頭で `Unload()` する（`Dat.cpp:8`）ため古い DAT は残らない |
| `0x07` が 0 件 | 正常系。ラベル `Schedule (0)`、リスト有効・空、ログ `[schedule] count=0` |
| Effect に `main` が無い | 先頭の `0x07` を既定にして再生し、ログに `[schedule] main なし` を出す（異常ではない） |
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
| `LoadDatFile`（`:282`） | `g_Effect.Build`（`main` 固定） | 既定 Schedule（`main`）を構築。**既定と同一なら再構築しない**。異なる選択時のみ `BuildFromChunk(name)` |
| `PlaySelectedAction`（`:648`） | `g_ActionNames` / NPC 専用 | `PlaySelectedSchedule` に改称し、**Effect でも `g_Effect.BuildFromChunk(name)`** で再生。NPC は `g_ActionCtx` のまま |
| `PanelProc`（`:686-704`） | `LBN_DBLCLK` のみ | `LBN_SELCHANGE` を追加（選択を pending に記録）、`IDC_SCHEDULE` の `LBN_SELCHANGE`/`LBN_DBLCLK`。**`WM_TIMER` は使わない** |
| 描画ループ（`:1166-1182`） | `g_Effect.Advance(g_Dt)` のみ | 毎フレーム `TickPendingCommit()` を呼び、7 コマ静止で確定（`kCommitDelayFrames`） |
| 新規ヘルパ | — | `ScheduleClear()` / `ClearSelection()` / `UpdateScheduleLabel()`（計 30 行程度） |

- `Effect.*` は **§4.8 の範囲（開始オフセット自動判定・`+0xAC` のテクスチャ割当）でのみ**変更する。
- `Dat.*` / `EffectVm.*` / `ListDb.*` / `Tim.*` / `Gfx.*` / `EffectBase.*` は変更しない。
- 検証モードの引数と既存出力書式は不変（`--action` の出力はそのまま回帰確認に使う）。
- 実装時は `ReadMe.md` の操作説明（「ダブルクリックで確定」→「単一選択で確定」、「Action」→「Schedule」）も更新する。

#### 4.7.8 検証

| 層 | 手法 | ゴールデン |
|---|---|---|
| 回帰（List） | `--list` | 29 グループ / NPC ROM1 = 1,266 件 |
| Schedule 列挙 | `--action "<dat>"`（名前なし＝`0x07` 一覧。**新規フラグなし**） | `ROM\131\6.DAT` = **39 件**、`ROM\11\17.DAT` = 1 件（main）、`ROM\119\85.DAT` = 5 件 |
| Effect のサブ Schedule | `--action "…\12\43.DAT" hit1` / UI で選択 | `main` 以外も timeline 非空で再生できる |
| 回帰（Effect） | `--shot "…\ROM\11\17.DAT" out.png` | timeline=**22**（prologue 4 + 本体 18）/ spawn=8 / 3.3333 s / 非背景 > 6,000 |
| UI 手動 | `NPC ROM1` → `ヒューム♀NPC (3-11)` を**シングルクリック** | ラベル `Schedule (n)`、リストに `0x07` 名が並ぶ |
| UI 手動（Effect） | `Black Magic` → `Fire II (11/18)` をシングルクリック → Schedule で `main` 以外があれば選択 | ラベル `Schedule (n)`、**選択のたびに再生が切り替わる** |
| UI 手動（失敗系） | 存在しない ROM / 壊れた DAT | 背景のみに戻り、ログに `clear:` |
| 性能 | ↑↓ 長押し **120 コマ（2 秒）**・連続クリック（60 fps 維持を計測） | §4.7.6 の目標値 |



---

### 4.8 Effect 資源の限定と開始オフセット（2026-11-09 解析の反映）

> 解析の詳細: `docs/ANALYSIS-Effect-Resource-Limiting.md`
> 根拠: AltanaView 0.6.2（`b5494a7c…`, D3D9）＋ 実 DAT 11 本の走査。

#### 4.8.1 確定した事実と現行実装の差分

| # | 事実（実測） | 現行実装 | 対応 |
|---|---|---|---|
| A | `0x07` の開始位置が **`+0x80` 固定**（効果 DAT） | **`+0x80` 固定**（`Effect.cpp:134-137`） | **`+0x50` 固定に統一**（§4.8.2。実測 1,806/1,848 = 97.7%） |
| B | テクスチャは generator の **`+0xAC` リンク名**で決まる | **`texIndex = 0` 固定**（`Effect.cpp:288`） | `+0xAC` → `0x20` を解決して割当（§4.8.3） |
| C | `+0xAC` は テクスチャ専用ではなく**リンク名**。参照先は `0x20`（テクスチャ）/ `0x1F`（ジオメトリ）/ `0x21`（テクスチャフレーム）/ `0x3D`（パラメータ）/ base / `dist`（予約語）/ `nulp`（参照先なし） | 未使用 | 型を指定して解決し、`0x20` のときだけテクスチャ（意味の確定は 解析 §7） |
| D | タイムラインは **DAT 内の全 generator を参照**（`11/17`=8/8, `11/18`=11/11, `11/22`=14/14） | 全索引 | 効果 DAT 内の motion 削減効果は**ほぼ無い** |
| E | base の **320 generator / 61 texture** を全構築する必要はない（`+0xAC` の異なり 91 種中 45 種のみ `0x20`） | 起動時に 320 索引 + 61 SRV | **索引のみ／SRV は遅延**（§4.6・§4.8.4） |

#### 4.8.2 開始オフセットの自動判定（A の修正）

**規則（確定）: `0x07` の命令列は `+0x50` から始まる**（Effect / NPC 共通）。`len == 0` で終端。

```
先頭は必ず prologue op（0x01 len2）→ 続けて 0x20(3) / 0x16(2) / 0x56 / 0x3C(4) / 0x23(3) / 0x0B(8) / 0x53(8) のいずれか
prologue を過ぎると本体命令（0x02 生成 / 0x03 / 0x2A / 0x31 / 0x32 …）が続く
len == 0 で終端（op == 0x00 も終端扱い）
```
実測: **1,848 チャンク中 1,806（97.7%）** が `+0x50` から正常終端。残り 42 は `+0x50` が `len==0`
（＝タイムラインでない `0x07`。例 `sum0`/`pip0`/`exe0`）→ 空タイムライン扱い＋ログ警告。

| 例 | `+0x50` の命令数 | 内訳 |
|---|---|---|
| `11/17` Fire | **22** | prologue 4 + 本体 18（従来の「18 命令」は prologue を除いた数） |
| `11/18` Fire II | 23 | prologue 3 + 本体 20 |
| `11/22` Blizzard | 23 | prologue 4 + 本体 19 |
| `12/43` hit1（サブ） | 15 | prologue 3 + 本体 12 |
| `119/85` iwa0（サブ） | 11 | prologue 2 + 本体 9 |

> 旧実装は `main` を `+0x80` 固定、それ以外を 7 候補の最長一致で判定していた（`Effect.cpp:42-77`, `:134-142`）。
> **両方とも廃止**し、`+0x50` 固定に統一する。`--selftest` の `prog=` は常に `0x50` を出す。
> サブ Schedule も同じ規則で読める（`ato/mai0/tgt0/ssub` 等で検証済み）。

#### 4.8.3 必要資源の算出（C / D / E）

```
link(g)  = chunk[g + 0xAC]（4 バイト名。同 DAT → base の順で解決）
needed05 = timeline の名前参照 op の名前（型 0x05 で解決）
needed20 = { link(g) | g ∈ needed05 かつ link(g) が 0x20 }                        … 直接（これが全て）
         ※ 0x19 = キー（時間,値）表 / 0x3D = パラメータ / 0x1F = ジオメトリ → **0x20 を要求しない**
         ※ 0x21 = テクスチャフレーム（スコープ外）。実測では追加の 0x20 を要求しない
         ※ dist = 予約語（距離ベース）/ nulp = 参照先なし → **解決対象外**（白テクスチャ）
```

> **Phase 1（実装計画 2026-11-09）でローカル `0x20`、Phase 2 で base の `0x20` も解決**する（`EffectBase::FindTexture` から SRV を借用。`--shot` の `srv=` で確認可能）。
> `link` が `0x20` 以外（`0x1F`/`0x21`/`0x3D`/予約語/未解決）を指す generator は **`tex = -1`（白テクスチャ）が正常**。

| DAT | generator 参照 | ローカル `0x20`（必要/総数） | base `0x20` | 直接参照なし |
|---|---|---|---|---|
| `11\17` Fire | 8 / 8 | `fai0`,`fai1`,`fai2`（**3 / 5**） | `hit6`, `hi17` | `fai3`, `f101` |
| `11\18` Fire II | 11 / 11 | `fire`,`fir2`,`fir3`（**3 / 7**） | `hi17` | `fir1`, `fir4`, `fir5`, `kemu` |
| `11\22` Blizzard | 14 / 14 | `pka1`,`kem2`（**2 / 6**、+`ice1` は `0x1F` 経由） | なし | `pke1`, `giz1`, `ft31` |

- 効果 DAT 内の motion は「ほぼ全部必要」。**削減は texture と base 索引で効く**。
- `fai3` / `fir5` / `kemu` / `pke1` などは**どの経路からも参照されない**（読み込み不要）。

#### 4.8.4 base の遅延ロード

- 起動時: `ROM\0\0.DAT` を読み、**名前→チャンクのハッシュ**（`0x05`/`0x07`/`0x20`）を作るだけ（1,105 件）
- `0x20`: SRV を生成し、**`+0xAC` が base を指す generator には `EffectBase::FindTexture` から借用**（実装済み。`--shot` で `srv` 非 0 を確認）
- `0x20`: SRV は**初回参照時**に生成。使用枚数は上表のとおり数枚（最大でも 61 枚全部ではない）。LRU で解放
- ROM 別ベース（R8）でも同じ方針（ベースごとにハッシュを持つ）
- 失敗時: 従来どおり警告のみで継続（R7）。テクスチャ生成失敗は白テクスチャで代替

#### 4.8.5 入れ子 Schedule（`op 0x03`）

`0x03` は「別種生成」だが、名前が **`0x07`（別 Schedule）** を指す場合は**その Schedule を入れ子で起動**する（2026-11-09 実装）。

- 例: `12/43`（Foe Requiem）の `main` は `0x03 name=hit1` を 1 命令だけ持ち、`hit1` が実際のエフェクトを生成する
  - `main` 単体では `spawn(op=0x02)=0`（＝何も出ない）→ `hit1` を起動して初めて描画される
- 実装: `StartSubTimeline(name)` が `0x07` を `+0x50` からパースし、`Delay` 加算で並行実行（同時実行数は 16 まで、同名は再起動しない）
- 入れ子側で実行する op は `0x02`（生成）/`0x2A`（色）/`0x3D`（ブロック読み飛ばし）。他は読み飛ばし
- 観測点: `--selftest` の **`subtl    = N`**（起動された入れ子 Schedule 数）。`12/43` は **1**

> `0x03` の名前が `0x05`（generator）を指す場合は従来どおり生成、どちらにも解決できなければ何もしない。

#### 4.8.6 AltanaView との表示差（現状・既知）

| 差分 | 内容 | 扱い |
|---|---|---|
| モデル非表示 | AltanaView は **キャラクター（PC/NPC モデル）＋エフェクト**を表示する（`Song12-43.gif` もモデル主体）。EffectTest はエフェクトのみ | P1（モデル描画）で解消予定 |
| 合成 | **解消（2026-11-09）**。AltanaView は `ALPHABLENDENABLE=1` / `SRCBLEND=SRCALPHA(5)` / `DESTBLEND=ONE(2)` / `ZWRITEENABLE=0` で描く（§4.8.6.1）。EffectTest は 3 モードを実装し既定を `alpha_add` に変更 | 対応済み |
| テクスチャ UV | `0x21` のレイアウトを確定し UV 矩形として実装（§4.8.6.2）。既定で適用し `--uv full` で従来動作。`12\43` は `0x21` を持たないため変化しない | 実装済み |
| prologue op | `0x01`/`0x20`/`0x23`/`0x0B`/`0x53` 等の意味が未確定で実行していない | 未対応（表示には不要な範囲あり） |

#### 4.8.6.1 合成モードの根拠（AltanaView 実測）

`AltanaView_unpacked.exe`（sha256 `b5494a7c…5cfae90`）の `IDirect3DDevice9::SetRenderState`（vtable `+0xE4`）呼び出しを静的に走査:

| アドレス | 設定 | 意味 |
|---|---|---|
| `0x40b8b0` | `SetRenderState(27, 1)` | `D3DRS_ALPHABLENDENABLE = TRUE` |
| `0x40b8ba` | `SetRenderState(19, 5)` | `D3DRS_SRCBLEND = D3DBLEND_SRCALPHA` |
| `0x40b8c7` | `SetRenderState(20, 2)` | `D3DRS_DESTBLEND = D3DBLEND_ONE` |
| `0x407fe1` | `SetRenderState(14, 0)` | `D3DRS_ZWRITEENABLE = FALSE` |
| `0x407fe9`～`0x407ff3` | `SetRenderState(27,1)` → `(19,5)` → `(20,6)` | モデル／通常透過用（`INVSRCALPHA`） |

- `SRCBLEND = ONE(2)` の設定は**存在しない** → 純加算（`ONE/ONE`）は AltanaView の描画経路ではない
- 走査結果: `SRCALPHA` 3 箇所、`INVSRCALPHA` 2 箇所、`DESTBLEND=ONE` 2 箇所

EffectTest 側は `GfxBlendMode`（`Gfx.h`）で 3 モードを保持し、既定を `GFX_BLEND_ALPHA_ADD`（`SRCALPHA/ONE`）とする。ピクセルシェーダは**非乗算色＋形状 alpha** を返し、ブレンド式に alpha を掛けさせる。

実測（4px 間引き 57,600 点、`--blend` で切替）:

| DAT | `add`（旧） | `alpha_add`（既定） | `alpha` |
|---|---|---|---|
| `11\17`（Fire） | 非背景 8,206 / maxSum **765** | 7,950 / **686** | 6,516 / 639 |
| `12\43`（Foe Requiem） | 13,150 / **765** | 13,150 / **560** | 13,150 / 469 |

`maxSum 765` は白飛び（白い板）。`alpha_add` で覆域をほぼ保ったまま階調が出る。

#### 4.8.6.2 テクスチャフレーム（`0x21`）のレイアウト

`0x21` は「テクスチャのどの矩形を貼るか」を持つブロックで、**1 フレーム = 6 頂点 × 24 バイトのクワッド**（2 三角形）である。

| オフセット | 型 | 内容 |
|---|---|---|
| `+0x12` | u16 | フレーム数（エントリ数） |
| `+0x14` | u8 | 0 以外ならエントリヘッダ長 = `0x14`（既定 `4`） |
| `+0x18` | char[8] | 先頭フレーム名（例 `fai01   `） |
| `+0x20` | char[8] | 最終フレーム名（例 `fai02   `） |
| `+0x28` | — | エントリ列。エントリ = ヘッダ（`+2` がクワッド数）+ `cnt × 0x90` |
| 頂点（24 B） | float[6] | `x, y, z, color(BGRA), u, v` |

実測（`11\17` = Fire）:

| ブロック | フレーム数 | UV（先頭 2 フレーム） | ローカル範囲 |
|---|---|---|---|
| `fai0`（32×32 DXT3） | 16 | `(0,0)-(0.246,0.246)` / `(0.25,0)-(0.496,0.246)` | `±2` |
| `fai1` | 1 | `(0,0)-(0.984,0.938)`（ほぼ全面） | `x -2..1.938, y -0.5..0.438` |
| `fai2`（256×256） | 1 | `(0,0)-(0.969,0.969)` | `±1` |

- 4×4 アトラスは `1/4 = 0.25` 刻みで、セルは `0.2461`（= 63/256）と 1/256 の余白を持つ
- `12\43`（Foe Requiem）には `0x21` が**存在しない**ため、`--uv 0x21` でも表示は変わらない

実装（`ParseTexFrames` → `GeneratorBlock::texFrames` → `MotionInstance::uv` → `Gfx::gUv`）:
- `--selftest` の generator 行に **`frm=N`**（`0x21` のフレーム数、無ければ `frm=0(none)`）
- インスタンス行に **`uv=(u0,v0,u1,v1)`**、`--shot` の inst 行に **`frm=現在/総数`**
- 既定で適用（`uvmode=0x21`）。`--uv full` で従来の全面サンプリングに戻せる
- フレーム選択は **`idx = (int)(経過秒 * 60) % frm`**（`sub.Elapsed` 基準・ループ）。AltanaView の該当オブジェクト（vtable `0x451e7c` → `FUN_0040bb80`）は
  エントリ（=フレーム）ごとに `SetTexture` / `SetTextureStageState` で描くことまで確認できたが、**時間→フレーム番号の式は未確定**のため暫定

| DAT | `--uv 0x21`（既定） | `--uv full`（従来） |
|---|---|---|
| `11\17` Fire | 非背景 8,050 / maxSum 348 | 8,033 / 348 |
| `11\18` Fire II | **2,694** / 576 | 2,396 / 570 |
| `11\22` Blizzard | 7,179 / 238 | 同左（`0x21` 未使用） |
| `12\43` Foe Requiem | 10,729 / 244 | 同左（`0x21` なし） |

#### 4.8.6.3 テクスチャ名は「グループ + specific」の 2 語

`0x20`（テクスチャ）のチャンク先頭 4 バイトは**グループ名の先頭 4 文字**で、実際の名前は `+0x11` の 16 バイトに **`<グループ 8><specific 8>`** として入る。

| DAT | チャンクタグ | 16 B 名（グループ / specific） | サイズ |
|---|---|---|---|
| `11\17` | `fai0` | `fai01` / `fai03` | 32×32 |
| `11\17` | `fai1` | `fai01` / `fai02` | 64×16 |
| `11\17` | `fai2` | `fai01` / `fai01` | **256×256** |
| `11\17` | `fai3` | `fai01` / `fai04` | 4192 B |
| `11\18` | `fire1`… | `fire1` / `fire11`〜`fire16`, `kemuri1` | 64×64 ほか |

`0x21` 側は `+0x18` = **グループ名**、`+0x20` = **specific 名**で、3 つの `0x21`（`fai0`/`fai1`/`fai2`）は**同じグループ `fai01` を共有**する。したがって

> **テクスチャは `0x21` の `+0x20`（specific 名）で選ぶ**（グループ名では 4 つに割れて特定できない）

- `0x21 fai0`（16 フレーム, 4×4 UV）→ specific `fai01` → **256×256** の `fai2` タグ
- `0x21 fai1` → `fai02` → 64×16 の `fai1` タグ
- `0x21 fai2` → `fai03` → 32×32 の `fai0` タグ

実装: `FindTextureIndexBySpecific()`（16 B 名の後半で照合）→ 失敗時は従来の 4 文字タグ照合。
`--shot` の inst 行 `tex=` はこの解決後の番号。

> 未確定: フレーム番号 → 時間の式（暫定 60 fps ループ）、および `+0x18`/`+0x20` が示す「フレームごとのテクスチャ切替」（`fire1`→`fire11` のような系列）の扱い。

#### 4.8.6.4 色順の混乱（PNG 保存バグ）— 真因と訂正

**訂正（2026-11-09）**: 一時「FFXI の TIM は BGRA なのでシェーダで R/B を入れ替える」と結論したが、**誤り**だった。真因は**`--shot` の PNG 保存**で、バックバッファ（DXGI `B8G8R8A8`）を WIC の RGBA として書き出していたため、PNG 上で R/B が入れ替わっていた。

- 決定的な証拠: `--bg 40F0F0`（R=64,G=240,B=240）を指定したのに PNG の背景が **`(240,240,64)`** になった。既定背景も `(13,15,23)` が `(23,15,13)` として保存されていた
- 修正: `GfxSaveBackBufferPNG()` で書き出し前に **R/B を入れ替えたコピー**を作る（`Gfx.cpp`）。これで `bg=(64,240,240)` と一致
- 結果: テクスチャは **RGBA として正しく読めている**（シェーダの色入替は不要）。`--color bgr` は**診断用**に残すが既定は `rgb`

> 教訓: 画素ゴールデンは「保存経路の色順」に依存する。**既知色の背景（`--bg`）を 1 点検査**してから数値を比較する（`tools\ShotStats.ps1` の `bg=` がその検査になる）。

#### 4.8.6.5 合成は「通常のアルファ」（透過なし参照で確定）

`fire2noalpha.mp4`（アルファ合成なしで描画した参照）を観察すると、**クワッドは巨大な黒板**になり、内側に炎の色だけが見えた。つまり

- テクスチャは **RGB ほぼ 0 ＋ alpha に形状**
- AltanaView はそれを **通常のアルファ合成**（`SRCALPHA` / `INVSRCALPHA`）で重ねている（煙が背景より暗くなる＝`dst*(1-a)` が効いている）

これに合わせ、EffectTest の既定を次へ変更した。

| 項目 | 変更前 | 変更後 |
|---|---|---|
| 既定合成 | `alpha_add`（`SRCALPHA`/`ONE`） | **`alpha`**（`SRCALPHA`/`INVSRCALPHA`） |
| ピクセルシェーダ | `max(rgb, alpha)` を輝度として加算 | **`rgb` と `alpha` をそのまま返す**（`t.rgb*col.rgb*1.5, t.a*col.a`） |
| 背景 | 固定 `(0.05,0.06,0.09)` | `--bg RRGGBB` で変更可（参照は `40F0F0`） |

新しいゴールデン（`--bg` 既定・4px 間引き）:

| DAT | 既定（`alpha` / `0x21`） | `--uv full` |
|---|---|---|
| `11\17` Fire | 非背景 8,050 / maxSum 348 | 8,033 / 348 |
| `11\18` Fire II | 2,694 / 576 | 2,396 / 570 |
| `11\22` Blizzard | 7,179 / 238 | 同左 |
| `12\43` Foe Requiem | 10,729 / 244 | 同左 |

合成モード別（Fire・`0x21`）: `add` 3,841 / 668、`alpha_add` 3,132 / 372、`alpha`（既定）8,050 / 348。

> `11\18`（Fire II）の大きな板には **4×4 の炎タイル**がそのまま見える。これは同 DAT の該当 generator が `0x21` を持たず、
> セル指定が **VM 命令側**（`0x21` 系 op）で行われるためと思われる。UV 矩形の適用自体は `0x21` を持つ DAT（`11\17`）で確認済み。



#### 4.8.7 検証（ゴールデン値）

**新規 CLI フラグは追加しない**（製品の CLI は現行 5 本のまま）。検証は既存出力への 2 語追加と、アプリ非依存の外部スクリプトで行う。

| 層 | 手法 | ゴールデン |
|---|---|---|
| 開始オフセット | `--selftest` のヘッダに `prog=0x..` を 1 語追加 | **常に `prog=0x50`**。命令数は `11\17`=22（prologue 4 含む）、`11\18`=23、`11\22`=23、`12\43` の `hit1`=15 |
| テクスチャ割当 | `--selftest` のインスタンス行に `tex=N` を 1 語追加 | `fai0`/`fai1`/`fai2` が generator ごとに異なる番号になる（0 固定でない） |
| 必要資源 | **外部スクリプト** `tools\EffectRes.ps1 <dat>`（DAT を直接解析する独立実装） | §4.8.3 の表（gen 8/8・tex 3/5、gen 11/11・tex 3/7、gen 14/14・tex 2/6） |
| 描画 | `--shot` | `11\17`: 非背景 > 6,000。`11\18`・`11\22` も**非ゼロ**（現行実装では 0 になる＝回帰検出） |
| 入れ子 Schedule | `--selftest` の `subtl    = N` / `--shot` の `inst=` | `12\43` は `subtl = 1`、`--shot` で `inst=8`・非背景 > 0 |

> アプリに期待値を出力させると同じバグを共有するため、必要資源の正解は**外部スクリプト側**で算出する（テストの独立オラクル）。
>
> テスト計画: `docs/TEST-PLAN-Schedule.md`（T-01〜T-12）／
> 検証計画: `docs/VERIFY-PLAN-Schedule.md`（V-01〜V-10）

---

### 4.9 エフェクト Schedule の実測カタログ（2026-11-09 走査）

> 解析の詳細: `docs/ANALYSIS-Effect-Resource-Limiting.md` / 走査スクリプト: `%TEMP%\av_eff2\scan2.ps1`（実装時に `tools\EffectRes.ps1` へ統合）
> 対象: `List/Effect/*.csv` 25 カテゴリ（`index.csv` 3 列目＝ROM。Effect2〜9 は ROM2〜9）

#### 4.9.1 規模

- CSV 行数 **3,745** → 重複除去・DAT 存在 **3,196 件**（DAT 不在 19 件）
- ROM 別: ROM1 2,725 / ROM3 229 / ROM4 119 / ROM2 41 / ROM6 24 / ROM5 23 / ROM7 18 / ROM8 17

| カテゴリ | 件数 | カテゴリ | 件数 |
|---|---|---|---|
| Effect1（FFXI） | 1,549 | SummoningMagic | 108 |
| Effect3（CoP） | 229 | Song | 106 |
| WhiteMagic | 195 | Item | 95 |
| BlueMagic | 189 | Ninjutsu | 42 |
| Ability | 174 | Effect2（Zilart） | 41 |
| BlackMagic | 166 | Unknown Effects | 41 |
| Effect4（Aht Urhgan） | 119 | Corsair / Skillchain / Automaton | 38 / 15 / 7 |

#### 4.9.2 Schedule（`0x07`）の実態 — 走査の主結果

- **81.4%（2,600 / 3,196）が `0x07` を 2 個以上持つ**。`main` だけの効果は少数派。
- `main` 以外の Schedule 名（出現数）: `tgt0` 454 / `trg0` 400 / `cas0` 362 / `cst0` 295 / `s000` 271 / `tgt1` 254 / `sub0` 194 / `damg` 168 / `cst1` 147 / `hit1` 144 / `atf0` 135 / `sub1` 135 / `vdam` 134 / `mot0` 133 / `hit0` 123

| 例 | Schedule 数 | 名前 |
|---|---|---|
| `11/17` Fire | 1 | `main` |
| `15/89` Berserk | 1 | `main` |
| `12/43` Foe Requiem | 2 | `main`, `hit1` |
| `10/9` ru_k | 3 | `main`, `mai0`, `tgt0` |
| `119/85` Meteor | **5** | `iwa0`, `main`, `ssub`, `iwa1`, `iwa2` |
| `0/63` Nanaa | 178 | 共有ライブラリ束（971 generator / 52 texture） |
| `0/61` `btl_` | 81 | 共有ライブラリ束（437 generator / 50 texture） |
| `0/0` `syst`（base） | 132 | 共有ライブラリ（320 generator / 61 texture） |

→ **設計 §4.7 の Schedule リストは `0x07` を全件列挙し、既定選択は `main`**（先頭行ではなく名前で選ぶ）。

> **役割（target / cast / damage 等）は命名からの推定であり未確定**。確定には AltanaView の呼び出し元解析が必要（§4.9.5-6）。

#### 4.9.3 尺（`main` ヘッダ +0x2C のフレーム数、60 fps 基準）

| 尺 | 件数 |
|---|---|
| 1–30 コマ（≤0.5 s） | 247 |
| 31–60 コマ（≤1 s） | 201 |
| 61–120 コマ（≤2 s） | 603 |
| **121–240 コマ（≤4 s）** | **1,233** |
| 241 コマ以上 | 492 |
| 0（未設定） | 311 |
| 不明（`0x07` 無し等） | 109 |

例: Fire 200 / Fire II 250 / Blizzard 271 / Meteor 139 / Foe Requiem 160 / Berserk 188。
※ 尺は**命令列を解析しなくても**ヘッダから取得できる。実装は `sum(delay)` で算出し、実測 4 件（Fire 200 / Fire II 250 / Blizzard 271 / Foe Requiem 160）でヘッダ `+0x2C` と一致する。

#### 4.9.4 資源規模（限定の妥当性）

| generator 数 | 件数 | | texture 数 | 件数 |
|---|---|---|---|---|
| 0 | 350 | | 0 | 140 |
| 1–4 | 198 | | 1–2 | 274 |
| 5–8 | 351 | | 3–5 | 815 |
| 9–16 | 1,274 | | 6–10 | 1,734 |
| **17 以上** | **1,023** | | 11 以上 | 233 |

`+0xAC` の参照種別（全 generator 合計）:

| 種別 | 件数 |
|---|---|
| `0x20`（同 DAT のテクスチャ） | **27,570** |
| その他（`0x1F` ジオメトリ / `0x21` フレーム / `0x3D` 等） | 13,823 |
| 空（`....`） | 3,748 |
| base の `0x20` | 2,582 |
| `dist`（予約語） | 852 |
| 未解決（`nulp` ほか） | **1,245 件の DAT が `nulp` を含む** |

→ 1 効果あたり 17 個以上の generator を持つものが 32%。**資源限定の効果は大きい**（base の 320 も同様）。

#### 4.9.5 設計への反映（要点）

1. **Schedule リスト = `0x07` 全件**（§4.7。**Effect / NPC 共通**）。既定は `main`。これで AltanaView と同じ「Schedule を選べる」状態になる。
   - 現行の不足は 3 点: **A** 構築が `main` 固定（`Effect.cpp:26-29`）、**B** UI に選択手段がない、**C** `main` の開始位置も `+0x80` 固定（`11/18`・`11/22` は 0 命令）
   - A/B は §4.7 で解消（`BuildFromChunk` は既存）。C は §4.8.2 で解消
2. **尺の表示**はヘッダ `+0x2C` から取得（命令列パース不要）。タイトル/ログに `dur=<frames>/60` を出せる
3. **資源限定**は意味が大きい（generator 17+ が 1,023 件、texture 6–10 が 1,734 件）
4. **`nulp` が 1,245 件**（最多の未解決）→ 白テクスチャ代替が常態（R10）。見た目差の主要因になり得る
5. **命令列の開始位置は `+0x50` 固定**（Effect / NPC 共通。実測 1,848 チャンク中 1,806 = 97.7% が正常終端）。旧 `main` の `+0x80` 固定と 7 候補の最長一致は**廃止**（§4.8.2）。`+0x50` が `len==0` の 42 件は timeline でない `0x07` として空扱い＋警告
6. サブ Schedule の**役割**（`tgt0`=target / `cas0`=cast / `damg`=damage 等）は**命名からの推定**。確定には AltanaView 側の解析が必要で、**同時再生は本設計の対象外**（手動選択で単体再生する）

#### 4.9.6 ゴールデン（代表 6 件）

| DAT | 名前 | Schedule | 尺 | gen | tex | `0x20` リンク |
|---|---|---|---|---|---|---|
| `11\17` | Fire | 1（main） | 200 | 8 | 5 | 4 |
| `11\18` | Fire II | 1（main） | 250 | 11 | 7 | 8 |
| `11\22` | Blizzard | 1（main） | 271 | 14 | 6 | 4 |
| `12\43` | Foe Requiem | 2（main,hit1） | 160 | 12 | 6 | 8 |
| `119\85` | Meteor | 5（iwa0,main,ssub,iwa1,iwa2） | 139 | 44 | 6 | 24 |
| `15\89` | Berserk | 1（main） | 188 | 12 | 3 | 9 |

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
- **起動直後に `ROM\0\0.DAT`（syst）が索引される**（1,105 チャンク / gen 320 / tex 61 / timeline 132）。描画はせず、**テクスチャ SRV も生成しない**（§4.8.4。初回参照時に生成）
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
- `Effect: Black Magic` → `Fire` で、EffectTest と**同一の数値**（**22 命令**＝prologue 4 + 本体 18 / 8 生成 / **3.3333 s = 200 コマ**）
- 画面にエフェクトが出る（加算合成・テクスチャ付き）
- **開始オフセットの統一（`+0x50`）**: `11\18`・`11\22` のような `+0x80` では 0 命令になる DAT も、`+0x50` 固定で正しく読める（§4.8.2）
- **テクスチャ割当**: generator の `+0xAC` により `fai0`/`fai1`/`fai2` が別テクスチャで描かれる
- **base テクスチャも解決**: `hit6`/`hi17` 等の base `0x20` は `EffectBase::FindTexture` から借用（`--shot` の `srv=` が非 0）
- **資源限定**: 未参照テクスチャ（`fai3` 等）を読み込まない（`--selftest` の `tex=` と外部スクリプトの必要集合が §4.8.3 と一致）
- **Schedule の切替**: `12\43`（Foe Requiem）で `hit1` を選ぶと `main` と別の再生になる（`BuildFromChunk` 経路の確認）
**検証**
```
ModelTest.exe --effect "ROM\11\17.DAT" --shot out_fire.png
期待: timeline=22（prologue 4 + 本体 18）/ spawn=8 / duration=3.3333 / 非背景 > 6000 サンプル

EffectTest.exe --selftest "ROM\11\17.DAT"
期待: prog=0x80 / timeline = 18 insts / spawn(op=0x02)=8 / インスタンス行の tex= が fai0,fai1,fai2 に対応

tools\EffectRes.ps1 "ROM\11\17.DAT"
期待: gens=8/8 / tex=3/5 (fai0,fai1,fai2) / base_tex=2 (hit6,hi17)
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
| Effect | ヘッドレス `--effect` | **22 命令**（prologue 4 + 本体 18）/ 8 生成 / 3.3333 s |
| 開始オフセット | `--selftest` の `prog=0x..` | `11\17`=0x80/18、`11\18`=0x74/20、`11\22`=0xA0/18 |
| 必要資源 | 外部スクリプト `tools\EffectRes.ps1 <dat>` | §4.8.3 の表と一致（gen 8/8・tex 3/5 等。`fai3` を除外） |
| Schedule 切替（Effect） | `--action "<dat>" <name>`（既存 CLI） | `12\43` の `hit1` が timeline 非空。`119\85` は `0x07` が 5 件 |
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
| R6 | ~~`0x07` の開始オフセットが DAT ごとに異なる~~ | — | **解消（§4.8.2）**: `0x07` は **`+0x50` 固定**（1,806/1,848 = 97.7%）。旧 `+0x80` 固定と候補探索を廃止 |
| R7 | **ベース `ROM\0\0.DAT` のロード失敗**（未インストール ROM / 破損） | 共有参照が解決できず一部エフェクトが欠ける | 警告ログのみで**起動継続**。個別 DAT 単独でも動作する設計にする |
| R8 | ROM 別 `0\0.DAT`（Effect7 の `0/0,Flowerpot` 等）の取り違え | 誤ったベースで解決 | カテゴリの ROM（`index.csv` 3 列目）に応じて `ROM<n>\0\0.DAT` を選ぶ。ROM1 を既定 |
| R9 | `+0xAC` が `0x21`（テクスチャフレーム）を指す場合 | 将来フレームアニメを実装したときの追加要求 | **意味は確定済み**（`0x21`＝フレーム。実測では追加の `0x20` を要求しない）。実装時は型 `0x21` を読み飛ばす |
| R10 | `dist` / `nulp` | 該当 generator がテクスチャ無しになる | **解決済み**: `dist` は距離ベースの予約語、`nulp` は参照先が存在しない。どちらも白テクスチャで代替しログに記録（解析 §7） |
| R11 | 遅延 SRV の寿命・上限 | メモリ増／解放漏れ | LRU（上限は実装計画で決定）。`EffectContext::ReleaseTextures()` で全解放（既存 API） |
| R12 | サブ Schedule（`tgt0`/`cas0`/`damg`…）の**役割**が未確定 | 意味を取り違える（表示自体は可能） | `prog` は **`+0x50` で確定**。役割は**命名からの推定**と明記（同時再生は非対象。確定には AltanaView 側の解析が必要） |

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


