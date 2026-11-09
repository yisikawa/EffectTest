# NPC モデル描画 検証計画（2026-11-09）

> 設計: `docs/DESIGN-Model-Render.md` / テスト: `docs/TEST-PLAN-Model-Render.md`
> 実装前のため **ゴールデン値は「実装後に記録」** とする（数値を先に書かない）。
> **制約: ModelTest（`D:\projects\ModelTest`）は一切変更しない**（参照のみ）。

## 1. 事前準備

```powershell
cd d:\projects\EffectTest
$exe = (Resolve-Path '.\x64\Release\EffectTest.exe').Path
$ROM = 'C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
Remove-Item effecttest_log.txt, effecttest_selftest.txt -ErrorAction SilentlyContinue
```

## 2. 検証項目

### V-M01 データ（ヘッドレス）

```powershell
$dat = Join-Path $ROM '131\6.DAT'
Start-Process $exe -ArgumentList @('--selftest', ('"'+$dat+'"')) -Wait
Start-Sleep -Milliseconds 300
Select-String -Path effecttest_selftest.txt -Pattern '^model|^prog|^timeline'
```
期待: `model: mesh>=1 poly>0 vertex>0 bones>0 tex>0`、`prog = 0x50`、`timeline = …`（既存と同一）

### V-M02 モーション一覧の一致

```powershell
Start-Process $exe -ArgumentList @('--action', ('"'+$dat+'"')) -Wait
Select-String -Path effecttest_log.txt -Pattern '^\[action\]'
```
期待: `0x2B` 名の集合が `--selftest` の `motion=` と一致

### V-M03 静止画

```powershell
Start-Process $exe -ArgumentList @('--model', ('"'+$dat+'"'), '--shot', 'm_npc.png') -Wait
Start-Sleep -Milliseconds 300
Select-String -Path effecttest_log.txt -Pattern '^\[model\]'
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path m_npc.png
```
期待: `[model] bones=… poly=… tex=…`、`nonBg > 1000`（実装後に実測して記録）

### V-M04 モーション

```powershell
foreach ($n in 0,30) {
  Start-Process $exe -ArgumentList @('--model', ('"'+$dat+'"'), '--motion', 'atk0', '--frames', "$n", 'm_p'+$n+'.png') -Wait
}
(Get-FileHash m_p0.png).Hash -ne (Get-FileHash m_p30.png).Hash
```
期待: `True`（骨が動いている）

### V-M05 背景色の整合（PNG 色順）

```powershell
Start-Process $exe -ArgumentList @('--model', ('"'+$dat+'"'), '--bg', '40F0F0', '--shot', 'm_bg.png') -Wait
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path m_bg.png
```
期待: `bg=64,240,240`

### V-M06 共存（P4 の土台）

```powershell
Start-Process $exe -ArgumentList @('--model', ('"'+$dat+'"'), '--effect', ('"'+$ROM+'\11\17.DAT"'), '--shot', 'm_both.png') -Wait
```
期待: モデルとエフェクトが同一フレームに描画され、`nonBg` がモデル単体より増える

### V-M07 回帰（エフェクト側が不変）

```powershell
foreach ($d in '11\17.DAT','11\18.DAT') {
  $p = Join-Path $ROM $d
  $o = 'rg_' + $d.Replace('\','_') + '.png'
  Start-Process $exe -ArgumentList @('--shot', ('"'+$p+'"'), $o) -Wait
  & powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path $o
}
```
期待: `11\17` = **8,050 / 348**、`11\18` = **2,694 / 576**（`docs/VERIFY-PLAN-Schedule.md` V-08 と一致）

### V-M08 ヘッドレス回帰

```powershell
foreach ($d in '11\17.DAT','11\18.DAT','11\22.DAT','12\43.DAT','162\50.DAT') {
  $p = Join-Path $ROM $d
  Start-Process $exe -ArgumentList @('--selftest', ('"'+$p+'"')) -Wait
  Start-Sleep -Milliseconds 300
  (Select-String -Path effecttest_selftest.txt -Pattern '^prog|^timeline|^spawn|^subtl').Line -join ' | '
}
```
期待: 既存の記録と一致（`11\17` 22/3.3333s/8/0、`11\18` 23/4.1667s/11/0、`11\22` 23/4.5167s/14/0、`12\43` 9/2.6667s/0/1、`162\50` 13/8.3333s/0/0）

## 3. 失敗時の切り分け

| 症状 | 疑う箇所 | 確認 |
|---|---|---|
| `[model] no mesh` | `0x2A` 読込（T-M03） | `--selftest` の `poly=` |
| 白一色 | テクスチャ（T-M02） | `tex=` と失敗件数のログ |
| 何も出ない | カメラ・AABB（T-M06/T-M07） | `GetBoneWorldAABB` のログ |
| エフェクトの数値が動く | `Gfx` の状態漏れ（T-M07） | V-M07 を先に実行 |

## 4. 記録テンプレート

| 実施日 | ビルド | V-M01 | V-M02 | V-M03 | V-M04 | V-M05 | V-M06 | V-M07 | V-M08 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|
| 未実施 | — | — | — | — | — | — | — | — | — | 実装後に記入 |
