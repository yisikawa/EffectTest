# テスト計画書 — モデル/エフェクト選択 → Schedule 表示（2026-11-09）

> 対象: `D:\projects\EffectTest`（EffectTest / Win32 + DirectX 11）
> 設計: `docs/DESIGN-List-To-Screen.md`（§4.7 Schedule / §4.8 資源とオフセット / §4.9 カタログ）
> 実装計画: `docs/superpowers/plans/2026-11-09-model-select-schedule.md`
> 解析: `docs/ANALYSIS-Effect-Resource-Limiting.md`
> 前提: 描画は D3D11・**60 fps**、待ちはコマ基準（7 コマ = 116.7 ms）、新規 CLI フラグは追加しない

## 1. 目的と範囲

| 項目 | 内容 |
|---|---|
| 目的 | `0x07`（Schedule）の読み取り位置・命令列・テクスチャ割当・UI 選択が設計どおりであることを、**症状ではなく原因**で判定する |
| 範囲 | Schedule 列挙、`+0x50` 命令列、`+0xAC` テクスチャ、必要資源、単一選択 UI、既存機能の回帰 |
| 非対象 | サブ Schedule の同時再生、`0x1F`（ジオメトリ）/`0x21`（フレーム）の描画、base テクスチャの遅延 SRV（Phase 2）、モデルのメッシュ描画（P1） |

## 2. テスト環境

| 項目 | 値 |
|---|---|
| OS | Windows 10 以降（x64） |
| GPU / API | DirectX 11（`d3d11.lib` / `dxgi.lib`）。`--shot` のみ GPU 必須 |
| ビルド | MSBuild v143 / **Release / x64** → `x64\Release\EffectTest.exe` |
| データ | `C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\...` |
| 検証用 DAT | `11\17`（Fire）/ `11\18`（Fire II）/ `11\22`（Blizzard）/ `12\43`（Foe Requiem）/ `131\6`（裏ゴブリン）/ `162\50`（timeline でない `0x07` を含む） |
| ログ | `effecttest_log.txt`（**追記**）/ `effecttest_selftest.txt`（上書き）/ `tools\EffectRes.ps1` の標準出力 |

> 注意: `effecttest_log.txt` は追記のため、判定前に**削除**する（前回実行の行が残ると誤判定になる）。

## 3. テストレベル

| レベル | 手段 | 依存 | 用途 |
|---|---|---|---|
| T1 独立オラクル | `tools\EffectRes.ps1 <dat>` | なし | 必要資源・命令列をアプリ非依存で算出（実装の正解） |
| T2 統合（ヘッドレス） | `--selftest` / `--action` / `--list` | なし | 命令列・`prog`・`tex`・Schedule 件数の回帰 |
| T3 描画 | `--shot` | GPU | 実際に絵が出るか（非背景画素） |
| T4 UI 手動 | GUI | GPU | 単一選択・Schedule 切替・失敗時の挙動 |

## 4. ゴールデン値（実測の根拠）

| DAT | 命令数 | `sum(delay)` | ヘッダ frames(+0x2C) | duration | spawn(0x02) | `tex=`（ローカル `0x20`） |
|---|---|---|---|---|---|---|
| `11\17` Fire | **22** | 200 | 200 | **3.3333 s** | **8** | 0=`fai0`, 1=`fai1`, 2=`fai2`。`fa04`(0x1F)/`hit6`・`hi17`(base) は **-1** |
| `11\18` Fire II | **23** | 250 | 250 | **4.1667 s** | **11** | 0/1/2（`fire`,`fir2`,`fir3`）。`3033`(0x3D)/`dist`/`hi17` は **-1** |
| `11\22` Blizzard | **23** | 271 | 271 | **4.5167 s** | **14** | 0=`pka1`, 2=`kem2`。`ic`系/`pkm2`/`ft32`/`giz3`/`kir1`/`nulp` は **-1** |
| `12\43` Foe Requiem `main` | 9 | 160 | 160 | 2.6667 s | 0 | — |
| `12\43` Foe Requiem `hit1` | 15 | — | — | — | — | — |

- **`tex=-1` は `0x20` 以外（`0x1F`/`0x21`/`0x3D`/base/予約語）を指す generator では正常**。白テクスチャで描画される
- 尺は実装が `sum(delay)` で算出し、実測 4 件でヘッダ frames と一致（200/250/271/160）

## 5. テストケース

### T1 独立オラクル（アプリ非依存）

| ID | 手順 | 期待 | 自動 |
|---|---|---|---|
| T-06a | `tools\EffectRes.ps1 -Dat "…\11\17.DAT"` | `SUMMARY … insts=22 gen=8/8 tex=3/5 base=2 unused=2` | ○ |
| T-06b | `tools\EffectRes.ps1 -Dat "…\11\18.DAT"` | `SUMMARY … insts=23 gen=11/11 tex=3/7 base=1 unused=4` | ○ |
| T-06c | `tools\EffectRes.ps1 -Dat "…\11\22.DAT"` | `SUMMARY … insts=23 gen=14/14 tex=2/6 base=0 unused=4` | ○ |

### T2 統合（ヘッドレス）

| ID | 手順 | 期待 | 自動 |
|---|---|---|---|
| T-01 | `--selftest "…\11\17.DAT"` → `prog` 行 | `prog      = 0x50` | ○ |
| T-02 | 同上 → `timeline` 行 | `timeline  = 22 insts, duration=3.3333s` | ○ |
| T-03 | 同上 → `spawn(op=0x02)` 行 | `spawn(op=0x02)=8` | ○ |
| T-04 | 同上 → `tex=` を含む行（frame 0〜11/59/119/239） | `tex=0`,`tex=1`,`tex=2` が出現。`fai0`→**`tex=2`**（specific `fai01` = 256×256）、`fai1`→`tex=1`、`fai2`→`tex=0` | ○ |
| T-05 | `--selftest "…\11\18.DAT"` | `timeline  = 23 insts, duration=4.1667s`（修正前は **0 insts**） | ○ |
| T-05b | `--selftest "…\11\22.DAT"` | `timeline  = 23 insts, duration=4.5167s`、`tex=0`/`tex=2` | ○ |
| T-07 | `--selftest "…\162\50.DAT"` | `sum0` 等の timeline でない `0x07` は **0 命令**（空扱い）。クラッシュしない | ○ |
| T-08 | `--action "…\131\6.DAT"`（名前なし） | `0x07` 一覧が **39 件**（出力書式は不変） | ○ |
| T-08b | `--action "…\12\43.DAT"`（名前なし） | `0x07` が **2 件**（`main`, `hit1`） | ○ |
| T-12 | `--list` | `groups=29`、NPC ROM1 = **1,266 件** | ○ |
| T-13 | `--selftest "…\12\43.DAT"` | `subtl    = 1`、`timeline  = 9 insts, duration=2.6667s` | ○ |
| T-14 | `--shot --blend add|alpha_add|alpha …\11\17.DAT` | `blend=` が指定どおり。maxSum が **765 / 686 / 639**（`add` のみ白飛び） | △（GPU 必須） |
| T-15 | `--selftest "…\11\17.DAT"` → gen 行の `frm=` | `g004`/`g001` = **`frm=16`**、`g000`/`g002` = **`frm=1`**、他は `frm=0(none)` | ○ |

### T3 描画

| ID | 手順 | 期待 | 自動 |
|---|---|---|---|
| T-09 | `--shot "…\11\17.DAT" out_fire.png` | `[shot] ... blend=alpha_add uvmode=0x21 inst=8 tex=5 ok=1`、非背景 **7,968** / maxSum **686**（4px 間引き 57,600 点） | △（GPU 必須） |
| T-09b | `--shot "…\11\18.DAT" out_fire2.png` | `ok=1` ＆ 非背景 **2,780** / maxSum 713（修正前は 0） | △ |
| T-16 | `--shot --uv full "…\11\18.DAT"` | `uvmode=full`、非背景 **2,484**（既定 `0x21` は 2,780） | △ |
| T-17 | `--shot --frames 60|120|180 "…\11\18.DAT"` | `frames=` が指定どおり。非背景 1,590 / 3,365 / 2,785（参照動画の 4.2 / 5.2 / 6.2 s に対応） | △ |

### T4 UI 手動

| ID | 手順 | 期待 | 自動 |
|---|---|---|---|
| T-10a | `B` でパネル → `Black Magic` → `Fire II (11/18)` を**シングルクリック** | ラベル `Schedule  (1)`、リストに `main`、タイトルが `11\18.DAT` | 手動 |
| T-10b | `Song` → `Foe Requiem (12/43)` → ラベルとリスト | `Schedule  (2)`、`main` と `hit1`。`main` が既定選択 | 手動 |
| T-10c | 続けて `hit1` を選択 | 再生が切り替わる。ログ `[schedule] effect hit1 insts=15` | 手動 |
| T-10d | グループを切り替える | Schedule が `Schedule  (-)` に戻り、無効化される | 手動 |
| T-10e | 起動直後（何も選ばない） | 背景のみ（座標軸・既定エフェクトなし） | 手動 |
| T-11 | 存在しない ROM を指す項目（例: ROM 未インストール）を選択 | 画面は**背景のみ**に戻り、ログに `[schedule] clear: resolve failed` | 手動 |
| T-11b | 壊れた DAT（0 バイト等）を選択 | 同上（`clear: load failed`） | 手動 |

## 6. 合否判定

- **自動（T1/T2）**: 期待文字列がログに**完全一致で出現**すること。1 件でも欠ければ不合格
- **描画（T3）**: `ok=1` かつ非背景 > 6,000（Fire）。GPU 不在環境では「未実施」として記録し、合格扱いにしない
- **UI（T4）**: 目視 + ログ（`[schedule] count=` / `effect ... insts=`）の両方で確認
- **回帰**: `--list` / `--action` / `--shot` の**出力書式が変わっていない**こと（値のみ変化）

## 7. 記録物

| 種別 | ファイル |
|---|---|
| 自己テスト | `effecttest_selftest.txt`（上書き） |
| 実行ログ | `effecttest_log.txt`（追記。判定前に削除） |
| オラクル | `tools\EffectRes.ps1` の標準出力（テキスト保存） |
| 描画 | `out_fire.png` / `out_fire2.png` |

## 8. リスクと代替

| # | リスク | 代替 |
|---|---|---|
| 1 | GPU 不在で T-09 が実施不能 | T2 の `inst`/`tex` 数で代替し、T3 は「未実施」と明記 |
| 2 | ROM 未インストール | `--list` は exit 2。T1/T2 は DAT パス直指定で実施可 |
| 3 | ログの追記で誤判定 | 判定前に `Remove-Item effecttest_log.txt` |
| 5 | 出力ファイルの読み取り競合（実測） | `--selftest` 直後の読み取りは前回内容を返すことがある。`Start-Sleep -Milliseconds 300` を挟み、`file` 行で対象 DAT を確認する |
| 4 | base テクスチャ未実装（Phase 2） | `tex=-1` を正常として扱う（設計 §4.8.3 の注記） |
