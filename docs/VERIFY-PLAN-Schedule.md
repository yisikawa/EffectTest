# 検証計画書 — モデル/エフェクト選択 → Schedule 表示（2026-11-09）

> 対象: `D:\projects\EffectTest`（Release / x64）
> テスト計画: `docs/TEST-PLAN-Schedule.md`（ケース ID を参照）
> 実装計画: `docs/superpowers/plans/2026-11-09-model-select-schedule.md`（タスク 1〜7）
> 設計: `docs/DESIGN-List-To-Screen.md`（§4.7 / §4.8 / §4.9）

## 1. 目的

本変更（`0x07` の `+0x50` 統一、`+0xAC` テクスチャ割当、Schedule 全件選択）が設計どおりであることを、**再現可能なコマンドと固定の期待値**で確認する。症状（「絵が出ない」）ではなく**原因（prog / 命令数 / tex）**で判定する。

## 2. 検証環境と前提

| 項目 | 値 |
|---|---|
| ビルド | `msbuild EffectTest.sln /p:Configuration=Release /p:Platform=x64`（v143 / C++17 / SDK 10.0） |
| 実行 | `x64\Release\EffectTest.exe`（GUI サブシステム。出力はファイル） |
| ROM | `C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM` |
| 時間基準 | 60 fps 固定（1 コマ = 16.67 ms）。待ちはコマ数（7 コマ = 116.7 ms） |
| 新規 CLI | **なし**（`--list` / `--base` / `--action` / `--selftest` / `--shot` のみ） |

## 3. 事前準備（必須）

```powershell
cd D:\projects\EffectTest
Remove-Item effecttest_log.txt -ErrorAction SilentlyContinue      # Log() は追記のため
Remove-Item effecttest_selftest.txt -ErrorAction SilentlyContinue
msbuild EffectTest.sln /p:Configuration=Release /p:Platform=x64
```
Expected: `Build succeeded`（エラー 0・警告 0 を目標）

```powershell
$ROM = 'C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
```

## 4. 検証手順

> 各コマンドで `EffectTest.exe` を実行した直後に `Start-Sleep -Milliseconds 300` を挟むこと（出力ファイルの読み取り競合。§8）。
> 判定前に `file      = ...` 行で**対象 DAT が一致**していることも確認する。

### V-01 `prog` が `0x50`（`11\17` / `11\18` / `11\22`）

```powershell
foreach ($f in '11\17','11\18','11\22') {
  .\x64\Release\EffectTest.exe --selftest "$ROM\$f.DAT"
  Select-String -Path effecttest_selftest.txt -Pattern '^prog' | Select-Object -ExpandProperty Line
}
```
Expected（3 行とも）: `prog      = 0x50`

### V-02 命令数と尺（T-02 / T-05 / T-05b）

```powershell
foreach ($f in '11\17','11\18','11\22') {
  .\x64\Release\EffectTest.exe --selftest "$ROM\$f.DAT"
  Select-String -Path effecttest_selftest.txt -Pattern '^timeline' | Select-Object -ExpandProperty Line
}
```
Expected:
- `11\17`: `timeline  = 22 insts, duration=3.3333s`
- `11\18`: `timeline  = 23 insts, duration=4.1667s`
- `11\22`: `timeline  = 23 insts, duration=4.5167s`

判定: 3 行一致。**`11\18` が 0 insts なら Task 1 未適用**と切り分ける。

### V-03 spawn 数（T-03）

```powershell
.\x64\Release\EffectTest.exe --selftest "$ROM\11\17.DAT"
Select-String -Path effecttest_selftest.txt -Pattern '^spawn' | Select-Object -ExpandProperty Line
```
Expected: `spawn(op=0x02)=8`

### V-04 テクスチャ割当（T-04 / T-05b）

```powershell
.\x64\Release\EffectTest.exe --selftest "$ROM\11\17.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'tex=' | Select-Object -First 12
```
Expected: `tex=0` / `tex=1` / `tex=2` が出現（`-1` のみなら Task 3 未適用）
補足: `tex=-1` は `0x1F`/`0x21`/`0x3D`/base/予約語を指す generator では**正常**

```powershell
.\x64\Release\EffectTest.exe --selftest "$ROM\11\22.DAT"
Select-String -Path effecttest_selftest.txt -Pattern 'tex=' | Select-Object -First 12
```
Expected: `tex=0`（`pka1`）と `tex=2`（`kem2`）が出現

### V-05 必要資源（T-06a〜c）

```powershell
foreach ($f in '11\17','11\18','11\22') {
  .\tools\EffectRes.ps1 -Dat "$ROM\$f.DAT" | Select-String 'SUMMARY'
}
```
Expected:
- `11\17`: `insts=22 gen=8/8 tex=3/5 base=2 unused=2`
- `11\18`: `insts=23 gen=11/11 tex=3/7 base=1 unused=4`
- `11\22`: `insts=23 gen=14/14 tex=2/6 base=0 unused=4`

### V-06 Schedule 列挙（T-08 / T-08b）

```powershell
.\x64\Release\EffectTest.exe --action "$ROM\131\6.DAT"
(Select-String -Path effecttest_log.txt -Pattern '\[action\]   ').Count
```
Expected: **39**（NPC の `0x07` 件数。出力書式は不変）

```powershell
.\x64\Release\EffectTest.exe --action "$ROM\12\43.DAT"
Select-String -Path effecttest_log.txt -Pattern '\[action\]   ' | Select-Object -Last 5
```
Expected: `main` と `hit1`（合計 2 件）

### V-07 timeline でない `0x07`（T-07）

```powershell
.\x64\Release\EffectTest.exe --selftest "$ROM\162\50.DAT"
Select-String -Path effecttest_selftest.txt -Pattern '^timeline'
```
Expected: `timeline  = 9 insts`（`main`）。`sum0` 等は対象外でクラッシュしない（終了コードが 0）

### V-08 描画（T-09 / T-09b・GPU 必須）

```powershell
.\x64\Release\EffectTest.exe --shot "$ROM\11\17.DAT" out_fire.png
Select-String -Path effecttest_log.txt -Pattern '\[shot\] dat='
```
Expected: `ok=1`。`out_fire.png` が生成され、非背景 > 6,000（画素検査は手動/別スクリプト）

```powershell
.\x64\Release\EffectTest.exe --shot "$ROM\11\18.DAT" out_fire2.png
```
Expected: `ok=1`（修正前は 0 命令で非背景 0）

### V-09 GUI（T-10a〜e / T-11）

```powershell
Remove-Item effecttest_log.txt -ErrorAction SilentlyContinue
.\x64\Release\EffectTest.exe
```
1. `B` でパネル表示 → `Black Magic` → `Fire II (11/18)` をシングルクリック
   Expected: ラベル `Schedule  (1)`、`main` が既定選択、タイトル `11\18.DAT`
2. `Song` → `Foe Requiem (12/43)` → ラベル `Schedule  (2)`、`main` と `hit1`
3. `hit1` を選択 → 再生が変わる。ログに `[schedule] effect hit1 insts=15`
4. グループを切り替え → `Schedule  (-)` に戻る
5. 起動直後（未選択）→ 背景のみ（座標軸・既定エフェクトなし）
6. 存在しない ROM を選ぶ → 背景のみに戻り、ログ `[schedule] clear: resolve failed`

### V-10 回帰（T-12）

```powershell
.\x64\Release\EffectTest.exe --list
Select-String -Path effecttest_log.txt -Pattern '\[list\] groups=|NPC ROM1'
```
Expected: `groups=29`、NPC ROM1 = `items=1266`

### V-12 入れ子 Schedule（T-13）

```powershell
$exe = (Resolve-Path '.\x64\Release\EffectTest.exe').Path
$dat = 'C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM\12\43.DAT'
Start-Process -FilePath $exe -ArgumentList @('--selftest', ('"' + $dat + '"')) -Wait
Select-String -Path effecttest_selftest.txt -Pattern '^subtl|^timeline'
Start-Process -FilePath $exe -ArgumentList @('--shot', ('"' + $dat + '"'), 'out_song.png') -Wait
Select-String -Path effecttest_log.txt -Pattern '\[shot\] dat='
```
Expected:
- `subtl    = 1`（`main` が `hit1` を起動）
- `timeline  = 9 insts, duration=2.6667s`（`main` 単体は `spawn(op=0x02)=0`）
- `[shot] … inst=8 … ok=1`、`out_song.png` の非背景 > 0

> **GUI アプリの呼び出し**: `& .\EffectTest.exe …` は終了を待たないため `Start-Process -Wait` を使う。
> `-ArgumentList` は自動クォートしないため、空白を含むパスは `'"' + $path + '"'` で囲む。

## 5. ゴールデン表（数値の根拠）

| 指標 | 値 |
|---|---|
| Fire 命令数 / 尺 / spawn | 22 / 3.3333 s / 8 |
| Fire II 命令数 / 尺 | 23 / 4.1667 s |
| Blizzard 命令数 / 尺 | 23 / 4.5167 s |
| Fire の `tex=` | 0（`fai0`）, 1（`fai1`）, 2（`fai2`） |
| Blizzard の `tex=` | 0（`pka1`）, 2（`kem2`） |
| 必要資源（Fire） | gen 8/8, tex 3/5, base 2, unused 2 |

## 6. 失敗時の切り分け

| 症状 | 疑う箇所 | 確認 |
|---|---|---|
| `11\18` が 0 insts／`prog` が出ない | Task 1（`+0x50`）/ Task 4（`prog=` 出力） | `Effect.cpp` の `progStart`、`RunSelfTest` の `fprintf` |
| `tex=-1` が全行 | Task 2（`tex=` 出力）/ Task 3（`+0xAC`） | Task 3 Step 7 の想定値と比較 |
| `EffectRes.ps1` とアプリで数値が違う | Task 6（スクリプト）/ Task 1 | `insts` の一致を先に確認 |
| `--action` の件数が 39 でない | 既存機能の回帰 | `--action` 出力書式の変更有無 |
| `--shot` が `ok=0` | GPU/PNG 保存（既存） | `effecttest_shot_diag.txt` |
| GUI で Schedule が空 | Task 5（列挙・有効化） | ログ `[schedule] count=` |

## 7. 記録テンプレート

| 実施日 | ビルド | 実施者 | V-01〜V-10 | 差分/備考 |
|---|---|---|---|---|
| YYYY-MM-DD | Release x64 / `<git hash>` | | 合格数 / 10 | 例: V-08 は GPU 不在で未実施 |

判定は「全 V が合格」または「未実施を明示して合格数で記録」。**未実施を合格に数えない。**

### 7.1 実施記録（2026-11-09 / `feature/schedule`）

| ID | 結果 | 実測値 |
|---|---|---|
| V-01 | ✅ | `prog = 0x50`（11/17, 11/18, 11/22） |
| V-02 | ✅ | 22 / 23 / 23 insts、3.3333 / 4.1667 / 4.5167 s |
| V-03 | ✅ | `spawn(op=0x02)=8` |
| V-04 | ✅ | Fire `tex=0,1,-1,2` / Blizzard `tex=0,-1,2`（`-1` は `0x1F`/base 依存で正常） |
| V-05 | ✅ | `22 gen=8/8 tex=3/5 base=2 unused=2` / `23 11/11 3/7 1 4` / `23 14/14 2/6 0 4` |
| V-06 | ✅ | NPC `131/6` = 39 件、`12/43` = `main,hit1` |
| V-07 | ✅ | `prog=0x50`、`timeline = 13 insts, duration=8.3333s`、exit 0（timeline でない `0x07`（`sum0` 等）を含む DAT でもクラッシュしない） |
| V-08 | ✅ | `ok=1`。非背景 **8,199**（Fire）/ **2,738**（Fire II）。最大 RGB 和 765 / 719 |
| V-11（Phase 2） | ✅ | `--shot` で `[base] … tex=61 load_ms=4`、`g006 link=hit6 srv≠0` / `g005 link=hi17 srv≠0`（base の `0x20` を借用）。`gs00`(0x3D)・`g003`(0x1F) は `srv=0`（白）で正常 |
| V-12（入れ子） | ✅ | `12\43`: `subtl = 1`（`main`→`hit1`）、`timeline = 9 insts`、`--shot` で `inst=8 ok=1`・非背景 **13,150**（修正前は main 単体で 0） |
| V-09 | 未実施 | GUI のクリック操作（手動） |
| V-10 | ✅ | `groups=29` / `NPC ROM1 items=1266` |

→ **自動検証 12 項目中 11 合格・1 未実施（V-09 の GUI 操作のみ手動）**。`--shot` の画素は 4px 間引き 57,600 点中の非背景数。


## 8. 制約・再現性

- `--shot` のみ GPU 依存。GPU 不在環境では V-08 を未実施とし、V-02/V-04 で代替する
- **実行直後のファイル読み取りは競合する**（実測: `--selftest` の直後に `Select-String` すると**前回の内容**が返ることがある）。各実行の後に `Start-Sleep -Milliseconds 300` を挟むか、`file` 行で対象 DAT を確認してから判定する
- `effecttest_log.txt` は**追記**のため、各判定前に削除する（前回行の混入を防ぐ）
- 60 fps 前提。モニタが 60 Hz でない場合、コマ基準の UI 待ち（7 コマ）は時間換算が変わるが、本検証のゴールデンは**命令数・尺・tex**であり影響しない
- base テクスチャ（`hit6`/`hi17`）は Phase 2 のため、`tex=-1` を正常として扱う（設計 §4.8.3）

