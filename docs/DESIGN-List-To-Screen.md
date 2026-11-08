# List 選択 → 画面表示 設計書（v1 提案）

> 作成: EffectTest / ModelTest の実測に基づく設計提案。実装前のレビュー用。
> この文書は「設計」であり、タスク分解の最終形は実装計画（別紙）で詰める。

## 0. 前提と用語

- **List** = `ModelTest\List`（`NPC_ROM1..9.lst`、種族別 `.lst`）と `AltanaView\List\Effect`（`index.csv` + カテゴリ CSV）。
  形式は `ID,表示名`（ID は `11/17` または `1-131-6`）。
- **Effect** = エフェクト DAT（`0x07`=`main` タイムライン、`0x05`=ジェネレータ）
- **モデル** = NPC/PC の DAT（`0x2A`=メッシュ、`0x29`=ボーン、`0x2B`=モーション、`0x20`=テクスチャ）
- **Action** = NPC DAT 内の `0x07` ブロック（`atk0`, `cast`, `vatk` …）。中は `0x03/0x57/0x0A` で
  `0x2B`/`0x3D` のモーション名を参照する「振り付け」。
- **Base（ベースエフェクト）** = `ROM[n]\0\0.DAT`（先頭ブロック名 `syst`）。
  `List/Effect/*.csv` の先頭行 `0/0,syst` がこれ。**全エフェクトが共有する土台**
  （実測: 539,776 B / 1,105 チャンク / `0x05`=320 ジェネレータ / `0x20`=61 テクスチャ / `0x07`=132）。
  個別エフェクト DAT に無い名前は、このベースから解決する。

## 1. 目的・スコープ

### 目的
List から選んだ項目を**そのまま画面に表示**する。Effect なら再生、NPC ならモデル表示＋Action 再生。

**表示の規約（重要）**
- **起動時は何も表示しない**（背景のみ。座標軸・グリッド・既定モデル/エフェクトも出さない）。
- **表示のトリガーは List の項目選択**（ダブルクリック＝確定）。Action の選択は、既に表示中のモデルの動きを切り替える操作。
- ベースエフェクト（`0/0`）は起動時に読むが**索引のみ**で、描画は一切しない。

### スコープ内
- **起動時にベースエフェクト（`ROM\0\0.DAT`）を必ず先読み**（詳細は §4.6）
- List のグループ／項目／Action の 3 段選択 UI
- NPC 項目 → モデル（メッシュ・ボーン・テクスチャ）表示
- Action 項目 → 対応モーションの再生（骨アニメーション）
- Effect 項目 → 既存の effect motion 再生（タイムライン + 2 段 VM）
- Effect と NPC の**同時再生**（P4、任意）

### スコープ外
- FBX / MQO / X 形式のエクスポート
- 4 面図保存・シャドウマップ等の ModelTest 既存高機能（壊さないが拡張しない）
- 0x1F（`f101.m`）ジオメトリの解読（別課題）

## 2. 現状の実測（判断材料）

| 項目 | EffectTest（現行） | ModelTest（参照実装） |
|---|---|---|
| 描画 | D3D11 自作ミニマム（`Gfx.cpp` 18KB、ビルボードのみ） | D3D11 フル（`Dx.cpp` 37KB、`Render.cpp` 18KB、スキニング/影/トゥーン） |
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
- Action（`0x07`）の参照名は **先頭 3 文字一致**で解決。例：`atk4`→0x3D、`vatk`→0x07（入れ子 Action）。
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
│   └─ 追加: ListPanel（グループ／項目／Action の 3 コンボ）  ★新規   │
│                                                                    │
│  選択ハンドラ（★新規: ListBridge.cpp）                             │
│   ├─ NPC 項目 → CModel::SetBody/LoadNPC/InitialTransform/          │
│   │              ConvertMesh          （既存 API を呼ぶだけ）       │
│   ├─ Action   → ActionProgram（★新規）→ 0x2B 名解決 →             │
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
項目 dblclick  → ListDb::ResolvePath → "…\ROM\131\6.DAT"
              → CModel::SetBody(dno); LoadNPC(); InitialTransform(); ConvertMesh()
              → DAT の 0x07 名（39 個）を Action combo へ
Action 選択    → ActionProgram::Parse(dat, "atk0")   … 命令列 6 件
              → 参照名の**先頭 3 文字**で同一 DAT の 0x2B を前方一致解決
                 （"vatk" は 0x07 の入れ子 Action、"hwat" は一致無し＝無視）
              → CModel::SetMotionName(name); LoadNPCMotion(); SetTime(0)
```
**起動時（必須・毎回）**
```
WinMain 起動 → FFXI 検出 → List 読み込み（29 グループ）
             → EffectBase_Load("<FFXI>\ROM\0\0.DAT")        … syst = 共有ベース
                 ・0x05 320 個のジェネレータを名前索引（"hit1","eis1" …）
                 ・0x20  61 枚のテクスチャを SRV 化
                 ・0x07 132 個のタイムライン名を索引（入れ子 Action 用）
              → 描画は行わない（空のシーン：背景のみ・座標軸も非表示）
```
> 起動時は**表示ゼロ**。表示は **List の項目選択（ダブルクリック）** がトリガー。
> ベースの読み込みに失敗しても**起動は継続**し、個別 DAT のみで動作する（警告ログ）。

**Effect を選んだとき**
```
グループ combo → ListDb::Items(group)            … Effect カテゴリ
項目 dblclick  → ResolvePath → "…\ROM\11\17.DAT"
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
| 項目リスト | `IDC_LIST_ITEMS` | `名前 (ID)` を列挙。ダブルクリックで確定 |
| Action | `IDC_LIST_ACTION` | NPC のときだけ有効。`0x07` 名を列挙 |
| Effect 同時再生 | `IDC_LIST_WITHEFFECT` | チェックで Effect を併走（P4） |

- 「表示切替」ラジオに **`List`** を 1 つ追加し、選択中は既存の種族/装備コンボを無効化する。
- 文字コードは既存の `ListDb`（UTF-8 / Shift-JIS 自動判定 → `wstring`）をそのまま使う。

### 4.4 ファイル構成（新規・変更）

| 種別 | パス | 責務 | 規模感 |
|---|---|---|---|
| 新規 | `ListDb.h/.cpp` | List 読み込み・ID→パス解決・文字コード変換 | 約 250 行（EffectTest から移植・依存なし） |
| 新規 | `ListPanel.h/.cpp` | 3 コンボの生成・イベント処理・選択結果の公開 | 約 250 行 |
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
> NPC 側（§4.2 の Action→モーション）は従来どおり**同一 DAT の `0x2B` を 3 文字一致**で解決し、
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
→ **起動時ロードのコストは 3 ms**。毎回読んでも体感影響なし（設計判断の裏付け）。

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
- `NPC ROM1` → `裏ゴブリン` を選ぶとモデルが画面に出る
- 既存の PC/NPC コンボによる表示が**従来どおり動く**（回帰なし）
- テクスチャが貼られている（白一色でない）
**検証**
```
ModelTest.exe --shot "ROM\131\6.DAT" out_npc.png
期待: 非背景ピクセル > 0（4px 間引きで数千）
```

### P2: Action 選択 → モーション再生（1 日）
**成果物**: `ActionProgram.h/.cpp`（`0x07` 解析・参照名解決）
**受け入れ基準**
- Action コンボに `0x07` 名が 39 件並ぶ（`ntob, pary, atk0, cast, vatk, dead …`）
- `atk0` 選択で命令列 6 件がログに出て、参照名 `hwat`/`vatk` が**先頭 3 文字一致**で判定される
  （`vatk`→0x07 入れ子、`hwat`→一致無し＝警告ログのみ）
- モデルが実際に動く（`MaxMotionTime() > 0`、再生で骨が変化）
- 併せて **0x2B の 3 文字一覧**（`at0,at1,at2,btl,cor,…`）も combo に出し、Action と独立に選べるようにする（ModelTest 互換）
**検証**
```
ModelTest.exe --action "ROM\131\6.DAT" atk0
期待: insts=6 / op=0x03 name=hwat / op=0x57 name=vatk
```

### P3: Effect 再生の移植（1〜1.5 日）
**成果物**: `EffectView.h/.cpp`, `EffectBillboard.h/.cpp`（EffectTest から移植）
**受け入れ基準**
- `Effect: Black Magic` → `Fire` で、EffectTest と**同一の数値**（18 命令 / 8 生成 / 3.3333 s）
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
- 60fps を維持（モデル 1 体 + エフェクト 8 インスタンス）
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
| Action 解析 | ヘッドレス `--action` | 命令数・`op`/`name` 列をゴールデン値と比較 |
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
| R3 | Action の未解読 op（`0x18`, `0x50`, `0x24`, `0x32`） | 一部の振り付けが再現されない | 命令長で読み飛ばし、ログに未対応 op を出す（数値は失わない） |
| R4 | 1,266 件のリスト投入が重い | UI が固まる | `WM_SETREDRAW` 抑制 + 遅延ロード（既に実装済みの方式を踏襲） |
| R5 | `EffectView` の SRV 所有権（ModelTest 側と二重解放） | クラッシュ | SRV は `EffectView` が所有し、`CMaterial` へは**借用**で渡す設計にする |
| R6 | モデル DAT ごとに `0x07` の開始オフセットが異なる | Action 解析失敗 | EffectTest で実装済みの**自動判定**（`main`=+0x80 / NPC=+0x50）を移植 |
| R7 | **ベース `ROM\0\0.DAT` のロード失敗**（未インストール ROM / 破損） | 共有参照が解決できず一部エフェクトが欠ける | 警告ログのみで**起動継続**。個別 DAT 単独でも動作する設計にする |
| R8 | ROM 別 `0\0.DAT`（Effect7 の `0/0,Flowerpot` 等）の取り違え | 誤ったベースで解決 | カテゴリの ROM（`index.csv` 3 列目）に応じて `ROM<n>\0\0.DAT` を選ぶ。ROM1 を既定 |

---

## 8. 意思決定が必要な点（レビュー依頼）

1. **土台アプリ**: 案 D（ModelTest を拡張）で進めてよいか？
   - 代替: 案 C（EffectTest にモデル描画を新規実装＝工数 2〜3 倍、リスク中）
2. **初期リリースに P4（同時再生）を含めるか**、まずは P1〜P3 で「List から選んで表示」を完成させるか
3. **Action → Effect の紐付け**（FFXI のデータに直接の対応表が無い）
   - (a) 紐付けしない（利用者が別々に選ぶ）
   - (b) 名前一致で自動（`Fire` と `casf` 等は一致しないため実質不可）
   - (c) 手動マップ CSV（`List/ActionEffect.csv` を新設し、`1-131-6,atk0,11/17` のように書く）
   - 推奨: **(a) で開始し、必要になったら (c)**


