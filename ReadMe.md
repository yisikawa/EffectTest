# EffectTest — FFXI エフェクト・モーション専用ビューア

`ModelTest` と同じ作法（Win32 + DirectX11 + フラット構成 + Visual Studio vcxproj）で作った、
**AltanaView 0.6.2 の effect motion だけを再現・可視化する**独立アプリです。

- モデルのメッシュ / スケルトン / モーション（`0x29`/`0x2A`/`0x2B`）は **扱いません**（ModelTest の担当）
- エフェクト DAT の **タイムライン**（`main`）と **2 段 VM**（ジェネレータ）だけを実行し、
  VM が出力する 位置 / スケール / 色 をビルボードで表示します

```
D:\projects\EffectTest\
├── EffectTest.sln / EffectTest.vcxproj
├── App.cpp         WinMain・ウィンドウ・入力・フレームループ・描画呼び出し
├── Gfx.cpp/.h      D3D11 初期化・ビルボード（SV_VertexID）・加算合成
├── Dat.cpp/.h      DAT 読み込み + チャンク走査（name/type/offset/size）
├── Tim.cpp/.h      テクスチャチャンク（0x20）→ SRV（DXT1/3/5 + 8bit パレット）
├── Effect.cpp/.h   エフェクトモデル（タイムライン / ジェネレータ / サブオブジェクト）
└── EffectVm.cpp    2 段 VM（VM#1 目標値評価 / VM#2 時間積分）
```

---

## ビルド

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
    'D:\projects\EffectTest\EffectTest.vcxproj' /p:Configuration=Release /p:Platform=x64
#   -> D:\projects\EffectTest\x64\Release\EffectTest.exe
```

- ツールセット: **v143**（VS2022）/ C++17 / MultiByte / x64
- 追加ライブラリ: `d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`, `comdlg32.lib`, `shell32.lib`
- 外部 SDK 不要（FBX SDK / DirectX SDK / DirectXTex はいずれも未使用）
- シェーダは実行時コンパイル（`Gfx.cpp` に HLSL を埋め込み）

---

## 使い方

```powershell
EffectTest.exe "C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\11\17.DAT"
```

> **起動時は何も表示しません**（背景のみ）。
> 表示のトリガーは **List パネルの項目の単一選択（クリック）** です（ダブルクリックも同じ）。
> 起動時に自動で何か表示したい場合だけ `EffectTest.exe --auto` を使ってください。

- 引数なしで起動 → `O` キーでファイル選択、または DAT をウィンドウへ**ドラッグ＆ドロップ**
- エフェクト DAT の例: `ROM\11\17.DAT`（Fire）, `ROM\11\18.DAT`（Fire II）,
  `List\Effect\BlackMagic.csv` などの `hi/lo` がそのままパスになります

| 操作 | 内容 |
|---|---|
| 左ドラッグ | カメラ回転（オービット） |
| ホイール | ズーム |
| `O` | DAT を開く |
| `Space` | 再生 / 一時停止 |
| `←` / `→` | 1 フレーム戻す / 進める（自動で一時停止） |
| `R` | 先頭へ |
| `L` | ループ切替 |
| `G` | 位置が 0 のインスタンスを横並び表示（確認用）切替 |
| `T` | トレース CSV（`effecttest_trace.csv`）出力切替 |
| `[` / `]` | ビルボードサイズ |
| `+` / `-` | 再生速度 |
| `1`〜`9` | 使用テクスチャ番号 |
| `W` | テクスチャを使わず白で描く（表示されない時の切り分け用） |
| `M` | 合成モードの切替（`alpha_add` → `add` → `alpha`、既定 `alpha_add`） |
| `X` | 座標軸（赤=X/緑=Y/青=Z）の表示切替 — **既定 OFF**（起動時は何も描かない） |
| `B` | List パネルの表示 / 非表示 |
| `Esc` | 終了 |

> 引数なしで起動した場合は **FFXI のインストール先をレジストリから自動検出**し、
> `ROM\11\17.DAT`（Fire）を自動で読み込みます。読込結果は `effecttest_log.txt` に記録されます。

タイトルバーに `ip（タイムライン位置）/ 経過秒 / インスタンス数` を常時表示します。

### ヘッドレス自己テスト（GPU 不要）

```powershell
EffectTest.exe --selftest "…\ROM\11\17.DAT"
#  -> カレントに effecttest_selftest.txt を出力
```

チャンク数・ジェネレータ一覧・タイムライン全命令・op ヒストグラム・240 フレームの
状態遷移をテキストで出力するので、**解析結果との突き合わせや回帰テスト**に使えます。

---

## List ブラウザ（NPC / Effect を名前で選択）

`ModelTest` の `List` と AltanaView の `List/Effect` を `EffectTest\List` にコピーしてあり、
起動すると右側に **List パネル**（別ウィンドウ）が開きます（`B` キーで表示切替）。

```
┌─ List (グループ / 項目 / Schedule) ─┐
│ [グループ ▼]  NPC  ROM1 / Effect: Black Magic …   │
│ ┌─ 項目（名前 と hi/lo） ─────────┐ │
│ │ Fire            (11/17)          │ │  ← 単一選択で確定
│ │ 裏ゴブリン      (1-131-6)        │ │
│ └──────────────────────────────────┘ │
│ Schedule  (1)                             │
│ ┌─ main / tgt0 / hit1 …（0x07 全件） ┐ │
└──────────────────────────────────────┘
```

| 操作 | 内容 |
|---|---|
| グループ | `NPC ROM1`〜`ROM9`（`List\NPC_ROM*.lst`）と `Effect: <カテゴリ>`（`List\Effect\index.csv`） |
| 項目を単一選択 | `hi/lo` から `ROM[n]\<hi>\<lo>.DAT` を解決して読み込み（**7 コマ静止後に確定**） |
| Schedule（Effect / NPC 共通） | その DAT 内の **`0x07` ブロック名を全件**列挙（既定は `main`）。選ぶと再生（Effect は `BuildFromChunk`、NPC は `ActionCtx`） |

> **文字コード**: `List` の `.lst` / `.csv` は UTF-8 と Shift-JIS が混在し得ます。
> 読み込み時に BOM / UTF-8 妥当性で自動判定して **UTF-16 に変換**し、Win32 コントロールは
> Unicode 版（`CreateWindowExW` / `SendMessageW`）を使うため、システムの ANSI コードページに依存しません。
> ログ（`effecttest_log.txt`）は **UTF-8** で出力されます。

### Effect と NPC の違い（解析結果）

| DAT 種別 | `0x07` の意味 | 中身 |
|---|---|---|
| **Effect**（例 `11/17` = Fire） | `main` = エフェクトのタイムライン | `0x05` ジェネレータ（`g000`…）を時間差で生成 → **本ビューアが描画する対象** |
| **NPC**（例 `1-131-6` = 裏ゴブリン） | **アクション**（`atk0`, `cast`, `vatk` …） | `0x03`/`0x57`/`0x0A` で **モーション名（`hwat`, `atk1`〜`atk4`）** を参照。`0x2B`（18個）が実モーション |

- NPC DAT にエフェクト生成（`0x05`）は**含まれません**。したがって「NPC の action の効果」を見るには、
  **Effect 側の項目を選んで再生**し、必要なら NPC のアクションを同時に進めてタイミングを比較します
  （NPC アクションは `0x18` などの未実装 op を含むため、現状は**命令列の可視化**まで）。
- 命令列は **`+0x50` から**（Effect / NPC 共通、`len == 0` で終端）。先頭の数命令は prologue（`0x01`/`0x20`/`0x3C`/`0x23` 等）。
  実測: `atk0` = 6 命令（`hwat`,`vatk` を参照）/ `vatk` = 11 命令（`atk1`〜`atk4` を参照、`0x3D`/`0x3E` で囲まれた選択ブロック）。

### ヘッドレス検証モード

```powershell
EffectTest.exe --list                          # グループ/項目/解決パスのダンプ
EffectTest.exe --base                          # ベースエフェクト（ROM\0\0.DAT）の索引ダンプ
EffectTest.exe --action "<DAT>"                # その DAT の 0x07 ブロック一覧
EffectTest.exe --action "<DAT>" vatk           # 指定アクションの命令列
EffectTest.exe --selftest "<DAT>"              # Effect としてのタイムライン検証
```

- `--selftest` は **`prog=0x50`**（命令列の開始位置）と、各インスタンス行の **`tex=`**（`+0xAC` から解決した `0x20` の番号）・**`link=`**（`+0xAC` の 4 文字名）・**`uv=(u0,v0,u1,v1)`** を出力します。generator 行には **`frm=N`**（同名 `0x21` のフレーム数。無ければ `frm=0(none)`）が出ます。`tex=-1` は `0x1F`/`0x21`/`0x3D` を指す generator では正常（白）。**base（`ROM\0\0.DAT`）の `0x20` は `--shot` で `srv=` が非 0 になることで確認**できます。
- 出力ファイルは**上書き**ですが、実行直後の読み取りは競合することがあるため、`Start-Sleep -Milliseconds 300` を挟むか `file      = ...` 行で対象 DAT を確認してください。
- 必要資源（generator / texture / base）はアプリ非依存の `tools\EffectRes.ps1` で算出できます（`[res] SUMMARY ...`）。
- **入れ子 Schedule**: `op 0x03` の名前が `0x07` を指す場合、その Schedule を並行実行します（`--selftest` の **`subtl    = N`**）。例: `12\43` の `main` は `hit1` を起動（`subtl = 1`）。

### 起動時ベースエフェクト（`ROM\0\0.DAT` = `syst`）

`List/Effect/*.csv` の先頭行 `0/0,syst` が示すとおり、`ROM\0\0.DAT` は
**全エフェクトが共有する土台**（共通ジェネレータ・共通テクスチャ・共通サブエフェクト）です。
本アプリは **起動時に必ずこれを読み込み**、名前解決のフォールバック先として索引します。

```
[base] path=…\ROM\0\0.DAT chunks=1105 gen=320 tex=61 timeline=132 load_ms=3
```

| 項目 | 実測値 |
|---|---|
| サイズ / チャンク | 539,776 B / **1,105** |
| `0x05` ジェネレータ | **320**（`hit1`,`eis1`,`hi10` …） |
| `0x20` テクスチャ | **61**（`plig`,`spec`,`test` …／DXT3） |
| `0x07` タイムライン | **132**（`effe`,`pop1`,`ei11` …） |
| 起動時ロード時間 | **3 ms**（61 枚の SRV 生成込み） |

- ベースが無い/壊れている場合は**警告ログのみで起動継続**（個別 DAT 単独で動作）。
- ROM 番号ごとに別の `0\0.DAT` が存在するため（例 `Effect7` の `0/0,Flowerpot`）、
  カテゴリの ROM に応じた `ROM<n>\0\0.DAT` を追加ベースとして扱える設計です（`EffectBase::Load` を複数回呼ぶ）。

---

## 実装と AltanaView の対応

| AltanaView (0x40xxxx) | 役割 | EffectTest |
|---|---|---|
| `FUN_00405770` | DAT 読込 + チャンク索引 | `DatFile::Load()` |
| `FUN_00405900` | ブロック名 → チャンク解決 | `DatFile::Find()` / `EffectContext::FindGenerator()` |
| `FUN_0040d790` | エフェクトルート構築・ブロック走査 | `EffectContext::Build()` |
| `FUN_004103d0` | **タイムライン実行**（仮想関数・毎フレーム） | `EffectContext::Advance()` / `ExecInst()` |
| `FUN_0040d730` + `FUN_0040d5a0` | モーションオブジェクト生成（0x90 ヘッダ解釈） | `GeneratorBlock` + `MotionInstance::Reset()` |
| `FUN_0040e600` | サブオブジェクト一括更新 | `Advance()` 内のインスタンスループ |
| `FUN_0040e650` | **VM#1**（目標値評価） | `Vm1()`（EffectVm.cpp） |
| `FUN_0040f4b0` | **VM#2**（時間積分） | `Vm2()`（EffectVm.cpp） |
| `FUN_0040f820` | キーフレーム線形補間 | 未実装（op 0x4F はテーブル直接参照で代替） |
| `D3D` 描画部 | ビルボード / `SRCALPHA`+`ONE` 合成（`ALPHABLENDENABLE=1`, `ZWRITEENABLE=0`） | `Gfx::GfxDrawQuad()`（D3D9 → D3D11 で代替。既定 `alpha_add`） |
| `CData::LoadTextureFromFile` (ModelTest) | TIM デコード | `CreateTimTexture()` |

---

## データ仕様（実装で確定した値）

### チャンク走査（モデルと共通）
```
next = (u32@pos+4 >> 3) & 0x7FFFF0
type =  u32@pos+4 & 0x7F
next < 16 で終端 / pos += next
先頭 4 バイト = ブロック名（"main","g000","fai0" …）
```

### `0x07` = タイムライン（Schedule）
- **`+0x50` から命令列**（Effect / NPC 共通）、`len == 0` で終端（以降 0 埋め）
- 先頭は prologue op（`0x01` → `0x20`/`0x16`/`0x3C`/`0x23`/`0x0B`/`0x53` …）で、その後に本体（`0x02` 生成など）が続く
- 命令: `[op:8][len:8][pad:16][delay:16][param:16][name/rgba:4B]`（`+0x1E` に追加パラメータ）
- 次命令 = `+ (len & 0x1F) * 4` バイト
- 時間は **フレーム単位**（`delay / 60.0` 秒）。`time_left -= dt; while (time_left < 0) { 実行; time_left += delay/60; }`

### generator（type `0x05`）= モーション定義
| オフセット | 内容 |
|---|---|
| `+0x00` | ブロック名 |
| `+0x10` | ID（Fire では全ジェネレータ 1026） |
| `+0x40..0x4F` | 色（RGBA float）。**全 generator で (1,1,1,0)** |
| `+0x50..0x5F` | 色 2（RGBA float）。**全 generator で (1,1,1,1)**（＝行列ではない） |
| `+0x76` | フレーム数（`長さ = (n+1)/60` 秒） |
| `+0x78` | 要素数 − 1 |
| `+0x79` | フラグ（`==0x10`） |
| `+0x80/84/88/8C` | ペイロード内オフセット A/B/C/D（B = VM プログラム） |
| ペイロード | `+0x90` 以降（`size - 0x90` バイト） |

### サブオブジェクト（VM の出力先 / ストライド `0x108 + N`）
| オフセット | 内容 |
|---|---|
| `0x000` | float[16] 行列 |
| `0x080` | 位置 (float3) ← **ビルボードの中心** |
| `0x08C` | 移動/回転目標 (float3) |
| `0x098` | スケール (float3) ← **ビルボードの大きさ** |
| `0x0A4` | 色 (float4) ← **乗算色・アルファ** |
| `0x0EC` | 経過秒 |
| `0x0F0` | 正規化時間 0..1 |
| `0x0F4` | 前フレーム位置（速度差分） |
| `0x100` | ルート逆参照 |
| `0x108` | VM#1 の出力スロット（`dest = dword0 >> 13`） |

> 重要: VM#2 の「op 別アキュムレータ」は `0x78 + op*4` に置かれるため、
> **`0x80`(位置) / `0x98`(スケール) と同じ領域を共有**します。
> 本実装がフラットな float 配列でサブオブジェクトを持つのはこの挙動を再現するためです。

---

## 検証結果（`ROM\11\17.DAT` = Fire）

`--selftest` の出力より（解析レポート `_rea/EFFECT_MOTION_ANALYSIS.md` と一致）:

| 項目 | 結果 |
|---|---|
| チャンク数 | 30 |
| ジェネレータ | 8（g006, g005, gs00, g004, g000, g001, g002, g003） |
| タイムライン | **22 命令（prologue 4 + 本体 18）/ `op=0x02` が 8 個 / 合計 3.3333 秒** |
| op ヒストグラム | `0x00=1 0x02=8 0x03=2 0x18=1 0x25=2 0x2A=2 0x3D=1 0x3E=1` |
| 生成タイミング | g006 即時 → gs00 +1f → g000/g005/g001 +9f 同時 → g004/g002 +20f → g003 +50f |
| モーション出力例 | g001 が -Z 方向へ移動（t=0.83 で z=-5.0, t=1.83 で z=-11.0） |
|  | g003/g004 が時間とともに拡大（scale.x 3.8 → 12.5） |
|  | g000 の色 = **(0.78, 0.50, 0.20, 0.15)**（炎の橙・半透明） |

これらは「タイムライン → VM → 位置/スケール/色」の経路が実データで機能していることを示します。

---

## 忠実度（何が再現でき、何が未確定か）

### 再現できている（コードで確定）
- チャンク走査・ブロック名による相互参照
- `main` タイムラインの全命令（0x00/0x02/0x03/0x2A/0x3D/0x3E）と **delay 加算方式の時間進行**
- ジェネレータの 0x90 ヘッダ解釈（frames / count / flag / 4 オフセット）
- VM#1 の主要 op（0x02/0x09/0x0B/0x0F/0x12/0x16/0x0A/0x10/0x11/0x45/0x47/0x49/0x4F/0x67）
  - `0x0F` = **Scale 設定**、`0x0A` = **対称乱数**、`0x03/0x0C/0x13` = **dest への対称乱数**、
    `0x67` = **`pos = -dest × 総時間 × 60`**（いずれも AltanaView 実測に合わせて修正）
  - `0x06`（球面ランダム）は構造のみ判明・定数未確定のため**未実装**
    （試作時の実測: `11\17` 3,625 → 4,628。`docs/ANALYSIS-AltanaView-Draw.md` §10.6c）
- VM#2 の時間積分・ループ折返し・op 別アキュムレータのエイリアシング
- サブオブジェクトのバイト配置（`0x108 + dest*4`）

### 近似 / 未実装（意図的に明示）
| 項目 | 状況 |
|---|---|
| VM#1 `0x03/0x08/0x0C/0x41`（回転・補間系） | `0x03/0x0C/0x13` は AltanaView 実測どおり `dest += rand(-arg,+arg)` に修正済み。`0x08/0x41` は近似（`dest += arg`） |
| VM#2 `0x0F..0x21`, `0x25`, `0x27/0x28`, `0x2C`, `0x33/0x34-0x37/0x46` | 未実装（IP 前進のみ or 部分実装） |
| `FUN_0040f820` キーフレーム補間 | 未実装（`0x4F` はテーブル直読みで代替） |
| `0x1F`（`f101.m`）ジオメトリ | **実 3D 平行四辺形で描画**（`--geo on`。既定は off）。§下記参照 |
| `0x19` / `0x3D` ブロック | 未使用（チャンク索引には載る） |
| `0x21`（テクスチャフレーム） | パース済み・既定で UV 適用（`frm=` / `uv=`。`--uv full` で無効化） |
| タイムライン `0x05`（モデル生成） | 未実装（別 DAT の読み込みが必要） |
| 描画の厳密な設定 | 加算合成 + Z 書き込みなしで**仮定**（AltanaView の実設定は未確認） |
| テクスチャ選択 | DAT 内の 0x20 を順に列挙し `1`〜`9` で切替（元の割当規則は未確定） |

> つまり本アプリは「**effect motion の骨格を実データで動かして観察する装置**」であり、
> 最終的な絵の一致（パーティクル形状・ブレンド・Z 順）は今後の解析対象です。

### 既知の制限
- 戻るステップ（`←`）は先頭から再計算するため、長いエフェクトでは一瞬待たされます
- 位置が 0 のままのジェネレータ（Fire の g006/gs00/g005）は、未実装 op に依存している可能性があります
- ビルボードは常にカメラ正対（元の表現が不明なため）

### `0x1F`（ジオメトリ）の実装状況
- **ヘッダ 0x30 B 固定**、頂点は `0x30 + i*0x24`（36 B）、`三角形数(+0x16) × 3` 個
- 頂点レコード: `+0x00 half x / +0x02 half y / +0x04 half z`、**UV は `+0x1C half u` / `+0x20 half v`**
- 6 頂点（A=v0,B=v1,C=v2）から平行四辺形を作り、**3D のまま描画**（ビルボード軸は使わない）
- テクスチャは `+0x1E` の**先頭 8 文字（グループ名）**で `0x20` を解決
  （例: `0x1F 'ic'` = "ice1    " → `0x20` "ice1    ft3js   "）
- **既定は `--geo off`**（実ビルボードとの重ね合わせ順・合成設定が未確定のため）。
  `--geo on` で 0x1F の形状を追加描画、`--geoflat on` で水平クワッドを地面扱いにする
- 未確定: `+0x14`（`fire`=1.875 / `f101`=-1.87）の意味、`+0x02 y` に混じる異常値
  （457 / -11460 等）の扱い、UV をこの値で正規化すべきか
- **異常値の扱い（実測 285 チャンク）**: ある軸だけに `|v|≧100` の未初期化値が混入する
  （`syur` y=63%、`ic` y=18%、`fire` y=0%）。異常率が 30% を超える軸は **0 固定**、
  残りはクワッド内の有効値の**中央値で補完**する。`+0x14` は UV レンジ相当の定数
  （UV の生値と 0.2% 差なので正規化しない）
---

## 今後の拡張（優先順）

1. **連続放出（パーティクル・プール）の実装** ← 参照動画との差の最大要因
   - `FUN_0040e0b0`（放出ループ）/ `FUN_00410090`（粒子再初期化）の移植
   - 設計は `docs/ANALYSIS-AltanaView-Draw.md` §10.10（フィールド表つき）
2. `0x1F` の残課題（`+0x14` の意味、UV 正規化、`y` 異常値）と `--geo` 既定化の判断
2. VM の未実装 op（特に `0x25` と `0x0F..0x21` の角度補間）の確定 → 回転・フェードの再現
3. `0x19` キーフレーム表（`FUN_0040f820`）の実装 → 滑らかな補間
4. `res/`（2026 版リポジトリ同梱）の設定ファイル読込 → エフェクト毎の調整値
5. 複数エフェクトの一覧ブラウズ（`List\Effect\*.csv` 連動）

---

## 参考

- 解析レポート: `D:\tools\AltanaView-master\_rea\EFFECT_MOTION_ANALYSIS.md`
- 擬似C 抽出物: `%TEMP%\av_eff\*.c`
- 参照実装: `D:\projects\ModelTest`（DAT 走査・TIM デコードの作法を踏襲）
- AltanaView は 2006 年の Tiamat 作でソース非公開。本実装は**クリーンルーム実装**
  （仕様＝実 DAT の観測結果と逆コンパイルで得た構造のみを参照し、コードは流用していません）

---

## トラブルシュート（「何も表示されない」時の確認順）

1. **`effecttest_log.txt` を見る**
   - `[auto] FFXI = ...` / `[auto] loaded ...` が出ていれば DAT は読めています
   - `(no data)` のままなら DAT 未読込 → `O` キーで選択、またはウィンドウへ D&D
2. **座標軸（赤=X / 緑=Y / 青=Z）が出ているか**
   - 出ていない → D3D11 初期化失敗（`GfxInit` の MessageBox が出ます）
3. **`W` キーでテクスチャを無効化**
   - 白い板が出るなら「テクスチャが黒/透明」が原因
4. **`[` `]` でサイズ、ホイールでズーム**（インスタンスが小さすぎる/遠すぎる場合）

### 実際に踏んだ不具合と修正（v0.2）

| 症状 | 原因 | 修正 |
|---|---|---|
| 起動しても `(no data)` | 引数なし起動時に何も読み込まない / `DragAcceptFiles` 未呼び出し | **FFXI 自動検出 + 既定エフェクト自動読込**、D&D 有効化 |
| テクスチャ 0 枚 | `fourCC` のバイト順が逆（`'DXT3'` は `0x44585433`） | MSVC 複数文字リテラルで比較 |
| 軸は出るがエフェクトが出ない | 行列を**転置せず** cbuffer へ（HLSL は列優先） | `XMMatrixMultiplyTranspose` |
| エフェクトが真っ黒 | FFXI のエフェクトテクスチャは **RGB=0 で alpha に形状**を持つ | `max(rgb, alpha)` を輝度として扱う |
| **`--shot` の色が入れ替わる**（背景 R/B が逆） | バックバッファは BGRA だが PNG エンコーダは RGBA を要求 | 保存時に R/B を入れ替える（`GfxSaveBackBufferPNG`）。`--bg 40F0F0` で 1 点検査 |
| PNG 保存が `REGDB_E_CLASSNOTREG` | `CoCreateInstance` に interface IID を渡していた | `CLSID_WICImagingFactory` |
| **List が文字化け**（`繧ｪ繝ｼ繧ｯ` 等） | `.lst` は **UTF-8** なのに ANSI(CP932) の ListBox へ生バイトを渡していた | 読み込み時に **UTF-8 / Shift-JIS を自動判定して UTF-16 化**し、コントロールを **Unicode(W) 版**へ変更 |

### 検証（`--shot` による画素確認）

`ROM\11\17.DAT` を 90 フレーム進めて描画し PNG 化 → 4px 間引きで 57,600 点を検査:

| 指標 | 修正前（軸のみ） | 加算 `ONE/ONE`（旧既定） | **既定 `alpha_add` + `0x21`** |
|---|---|---|---|
| 背景以外のサンプル数 | 13 | 8,206 | **3,625** |
| 最大 RGB 合計 | 456 | 765（白飛び） | **348** |

スクリーンショット: `shot_fire.png`（`--shot` で再生成可能）

#### 合成モード（`M` キー / `--blend`）

AltanaView の `SetRenderState` を静的に走査すると、エフェクト描画は
**`ALPHABLENDENABLE=1` / `SRCBLEND=SRCALPHA(5)` / `DESTBLEND=ONE(2)` / `ZWRITEENABLE=0`** でした
（`AltanaView_unpacked.exe` の `0x40b8b0`〜`0x40b8ca`）。
ただし **透過なしで描画した参照（`fire2noalpha.mp4`）** ではクワッドが黒板になり、煙が背景を暗くすることから、
実際のエフェクト合成は **通常のアルファ**（`SRCALPHA`/`INVSRCALPHA`）と判断しました（`docs/DESIGN-List-To-Screen.md` §4.8.6.5）。

| モード | ブレンド式 | 用途 |
|---|---|---|
| `alpha_add` | `dst + src*srcA` | 加算寄り（比較用） |
| `add` | `dst + src` | 旧来の純加算（比較用） |
| `alpha`（既定） | `src*srcA + dst*(1-srcA)` | エフェクト（参照動画と一致。煙が背景を暗くする） |

- GUI: `M` キーで巡回。現在のモードはタイトルとログ `[blend] <mode>` に出ます
- ヘッドレス: `--shot --blend alpha|add|alpha_add "<dat>" out.png`（`[shot] … blend=<mode>` を出力）
- テクスチャフレーム: `--shot --uv 0x21|full "<dat>" out.png`（既定 `uvmode=0x21`。`full` で全面サンプリングに戻る）
- フレーム選択: `--shot --uvframe time|random "<dat>" out.png`（既定 `time`＝経過時間で進める／ゴールデン維持。`random`＝AltanaView 準拠で毎描画ごとに乱数。`11\17` 7,698 / `11\18` 2,411）
- モデル従属: `--shot --attached on|off`（`--model`/`--bone`/`--bone-map` 指定時は自動 on）
  `on` では AltanaView の `ctx+0x1a0 != 0` 相当として **VM#1 の op を実行しない**（位置は骨で決まる）
- 骨の自動配分: `--shot --bone-map auto`（体軸 Y が 0.05〜1.75 m の骨を等分に割り当てる。`11\18` 180 フレームで 8,513 / 748）
- **部位IDで自動割当**: `--model` を指定して `--bone` も `--bone-map` も書かない場合、既定で `--bone-map part`
  （`データ2 op 0x01` の `+0x06` 部位ID → 骨。`11\18` + `--model human --motion idl0` で 4,446 / 765）
- UV クロップ: `--shot --uvcrop on|off "<dat>" out.png`
  （`0x21` を持たないジェネレータの UV をテクスチャの不透明領域にする。既定 `off`）
- ブレンド自動: `--shot --blend auto "<dat>" out.png`（テクスチャの黒割合 > 0.5 で `add`、他は `alpha`）
- UV スクロール: `--shot --uvscroll off|on|cell "<dat>" out.png`
  （`on` = 累積値を 0..1 巡回、`cell` = 1 セル幅で巡回。既定 `off`＝ゴールデン維持）
- トレース: `--shot --trace "<dat>" out.png` → `effecttest_trace.csv`（列: `frame,instance,sub,name,elapsed,t01,px,py,pz,sx,sy,sz,r,g,b,a`。**サブ（粒子）単位**）
- 時刻指定: `--shot --frames N "<dat>" out.png`（既定 90 = 1.5 s。1 フレーム = 1/60 s）
- 骨格（GPU 不要）: `--skel "<model dat>" [motion4]`（骨数・モーション一覧・姿勢の変化をログ）
- 骨をエフェクトのルートに: `--shot --model human --motion <name4> --bone N "<effect dat>" out.png`
  （`--bone` だけなら基準モデル **human**（`0-27-82` → `ROM\27\82.DAT`）を自動使用）
- インスタンス毎の骨: `--shot --bone-map "0:10,1:20" --motion <name4> "<effect dat>" out.png`
  （骨は **0 始まりの配列番号**。`--bone` と独立で、未指定インスタンスは変換なし）
- `--size` の**既定は `meter`**（`0x21` のクワッド範囲＝約 2〜4 m。無ければ 1 m）。`scale`（VM の scl）は X 成分が膨張して帯状になるため既定から外した
  - 実測（`11\18`/180 フレーム）: `quad`=11,158 / `meter`=5,470 / `scale`=9,326 / `square`=4,454
  - **寸法は `quad` が参照と一致**（暗部 10,204 vs 参照 11,301）
- **ビルボード則**: 面は常にカメラ正対（骨行列は**中心位置にだけ**適用し、軸は回さない）。3D スケールは面内の幅・高さに効く
  - `--size quad` は `0x21` のメートル実寸のみを使う（VM の壊れた scl を掛けない → 帯状の黒塊が出ない）
- 背景色: `--shot --bg RRGGBB "<dat>" out.png`（参照動画は `40F0F0`）
- 色順（診断用）: `--shot --color bgr|rgb "<dat>" out.png`（既定 `rgb`。PNG 保存の色順は修正済み）
- 参照動画との比較: `ffmpeg -ss <3.2 + N/60> -i fire2.mp4 -frames:v 1 -vf "crop=620:900:640:90" cmp.png`（`docs/VERIFY-PLAN-Schedule.md` V-15）
- 画素統計: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\ShotStats.ps1 -Path out_fire.png`
  → `sampled=57600 nonBg=8050 maxSum=348`

| DAT | `add` | `alpha_add` | `alpha`（既定） |
|---|---|---|---|
| `11\17`（Fire） | 3,841 / 668 | 3,132 / 372 | **3,625 / 399** |
| `11\18`（Fire II） | — | — | **3,098 / 576** |
| `11\22`（Blizzard） | — | — | **5,686 / 247** |
| `12\43`（Foe Requiem） | — | — | **3,628 / 257** |

> `--uv full`（`0x21` を使わない）では Fire 8,033 / 348、Fire II 2,396 / 570。
> `--bg 40F0F0` で参照動画と同じシアン背景になり、そのまま見比べられる。
> `11\18` の板に 4×4 の炎タイルが見えるのは、その DAT の該当 generator が `0x21` を持たず、セル指定が VM 命令側にあるため。
>
> テクスチャ名は `0x20` の 16 B 名 = **`<グループ 8><specific 8>`** で、`0x21` の `+0x20`（specific）と照合して選ぶ
> （例: `0x21 fai0` → `fai01` = 256×256。従来はタグ一致で 32×32 を選んでいた）。

> `12\43` で見える**チェッカー模様の板**は、テクスチャアトラスの全面をそのまま貼っているためです
> `0x21` のフレーム／UV はパース済みで、既定で適用されます（`--uv full` で従来動作）。
> `12\43` には `0x21` が無いため、この指定でも表示は変わりません。



