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
Start-Process $exe -ArgumentList @('--skel', ('"'+$dat+'"'), 'idl0') -Wait
Start-Sleep -Milliseconds 300
Select-String -Path effecttest_log.txt -Pattern '^\[skel\]'
```
期待（実測値）:
- `[skel] … bones=56 motions=18 rot=mirrorY`（既定のルート変換は ModelTest と同じ `matrixMirrorY`）
- `[skel] rest t=0.000 root=(0.000,0.000,0.000) last=(-0.187,0.306,-0.525)`（`mirrorY` 適用後。bone1 は y=+1.151）
- `motion=idl0` の 3 行で `mid` が変化（時間で姿勢が変わる）
- ルート変換の比較: `--skel "<dat>" idl0 none|mirrorY|rotX180|rotZ180`（`rot=` が切り替わる）
- エフェクト回帰は不変（`11\17` = 8,050 / 348、`12\43` = `subtl=1`）

> `--skel` は GPU 不要。骨・モーションの読込だけを検証する。

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

### V-M09 骨をルートにしたエフェクト（`--model` / `--motion` / `--bone`）

```powershell
$eff = Join-Path $ROM '11\18.DAT'
$mdl = Join-Path $ROM '131\6.DAT'
Start-Process $exe -ArgumentList @('--shot','--model',('"'+$mdl+'"'),'--motion','idl0','--bone','10',('"'+$eff+'"'),'m_b10.png') -Wait
Select-String -Path effecttest_log.txt -Pattern '^\[shot\] model=|^\[shot\] root'
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path m_b10.png
```
期待（実測値）:
- `[shot] model=… bones=56 motions=18 motion=idl0 rot=mirrorY bone=10`
- `[shot] root bone=10 pos=(-0.020,0.016,0.260)`
- 非背景 **2,167**（`--model` なしは **2,694**、`--bone 0` は 458＝`mirrorY` で Y 反転）

### V-M10 基準モデル human の自動解決

```powershell
Start-Process $exe -ArgumentList @('--skel','human','idl0') -Wait
Select-String -Path effecttest_log.txt -Pattern '^\[skel\] dat=|^\[skel\] rest'
```
期待: `[skel] dat=…\ROM\27\82.DAT bones=94 motions=130 rot=mirrorY`、`rest … last=(0.000,0.090,1.000)`

```powershell
# --bone だけ指定 → human が自動で使われる
Start-Process $exe -ArgumentList @('--shot','--bone','10','--motion','idl0',('"'+$ROM+'\11\18.DAT"'),'m_h10.png') -Wait
Select-String -Path effecttest_log.txt -Pattern '^\[shot\] model=|^\[shot\] root'
```
期待: `model=…\ROM\27\82.DAT bones=94 motions=130 … bone=10`、`root bone=10 pos=(-0.034,-1.063,-0.059)`、非背景 **452**

### V-M11 インスタンス毎の骨（`--bone-map`）

```powershell
Start-Process $exe -ArgumentList @('--shot','--bone-map','0:10,1:20','--motion','idl0',('"'+$ROM+'\11\18.DAT"'),'m_bm.png') -Wait
Select-String -Path effecttest_log.txt -Pattern 'bone-map|model='
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path m_bm.png
```
期待（実測値）:
- `[shot] bone-map=0:10,1:20 mapped=2 inst=11`
- 非背景 **2,591**（なしは 2,694、`0:10,1:20,2:30,3:40` は 2,050）
- 骨は **0 始まりの配列番号**（`Skeleton::World(index)`）

### V-M12 寸法（メートル基準）と煙の面積

```powershell
$ROM='C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
foreach ($m in @('scale','square','quad','meter')) {
  .\x64\Release\EffectTest.exe --shot --size $m --uv 0x21 --frames 180 --bg 40F0F0 "$ROM\11\18.DAT" "m_$m.png"
  .\tools\ShotStats.ps1 -Path "m_$m.png" | Select-Object -Last 1
}
```
期待（実測値・2026-11-09）:

| `--size` | 非背景 | 暗部（lum<60） |
|---|---|---|
| `scale`（既定） | 9,326 | 405 |
| `square` | 4,454 | — |
| `quad` | **11,158** | **10,204** |
| `meter` | 5,470 | 4,388 |

- 参照フレーム（`fire2.mp4` 6.2 s = effect 3.0 s）の暗部は **11,301** → **`quad` が一致**
- **単位はメートル**（1.0 = 1 m、骨も同基準）。`0x21` のクワッドは `fire11`=2×4 m、`fire14`≈3.94 m 角
- generator ヘッダ `+0x40`/`+0x50` は**色**（全 generator で (1,1,1,0)/(1,1,1,1)）。寸法フィールドは無い

### V-M13 ビルボード則（軸を回さない）

```powershell
$ROM='C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
# 既定（回帰）: 2,694 のまま
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\11\18.DAT" reg_after.png
# 骨 10 をルートに（軸を回さないので面積が増える）
.\x64\Release\EffectTest.exe --shot --frames 90 --bone 10 --motion idl0 "$ROM\11\18.DAT" bone_after.png
# 0x21 実寸のみ（VM スケールを掛けない）
.\x64\Release\EffectTest.exe --shot --size quad --frames 180 --bg 40F0F0 "$ROM\11\18.DAT" bb_quad.png
```
期待（実測値・2026-11-09）:
- 既定 **2,694**（不変）
- `--bone 10` **2,886**（軸回転をやめた結果。以前は 2,167）
- `--size quad` は**帯状の黒塊が出ない**（円形の煙）。非背景 8,986 / 暗部 4,388
- 帯の原因は `scl.x` の破綻（7.60 / 27.47）。`0x21` の実寸（±2 m など）だけを使う

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
| 2026-11-09 | Release x64 | ✅ | — | — | — | — | — | — | — | **P1a 完了**: `--skel` で bones=56 / motions=18、ルート変換 `mirrorY`（ModelTest の `matrixMirrorY`）で bone1 y=+1.151、エフェクト回帰 8,050/348 不変 |
