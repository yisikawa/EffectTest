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
- エフェクト回帰は不変（`11\17` = 3,625 / 399、`12\43` = `subtl=1`）

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
期待: `11\17` = **3,625 / 399**、`11\18` = **3,098 / 576**（`docs/VERIFY-PLAN-Schedule.md` V-08 と一致）

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
- 非背景 **2,167**（`--model` なしは **3,098**、`--bone 0` は 458＝`mirrorY` で Y 反転）

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
- 非背景 **2,591**（なしは 3,098、`0:10,1:20,2:30,3:40` は 2,050）
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
# 既定（回帰）: 3,098 のまま
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\11\18.DAT" reg_after.png
# 骨 10 をルートに（軸を回さないので面積が増える）
.\x64\Release\EffectTest.exe --shot --frames 90 --bone 10 --motion idl0 "$ROM\11\18.DAT" bone_after.png
# 0x21 実寸のみ（VM スケールを掛けない）
.\x64\Release\EffectTest.exe --shot --size quad --frames 180 --bg 40F0F0 "$ROM\11\18.DAT" bb_quad.png
```
期待（実測値・2026-11-09）:
- 既定 **3,098**（不変）
- `--bone 10` **2,886**（軸回転をやめた結果。以前は 2,167）
- `--size quad` は**帯状の黒塊が出ない**（円形の煙）。非背景 8,986 / 暗部 4,388
- 帯の原因は `scl.x` の破綻（7.60 / 27.47）。`0x21` の実寸（±2 m など）だけを使う

### V-M14 複数サブ（粒子）

```powershell
$ROM='C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\11\17.DAT" ms_fire.png
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\11\18.DAT" ms_fire2.png
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\12\43.DAT" ms_bliz.png
Select-String -Path effecttest_log.txt -Pattern 'subs='
```
期待（実測値・2026-11-09）:
- `11\17` = **3,625 / 399**、`11\18` = **3,098 / 576**（**完全一致**＝サブ数 1 の経路は従来と同一）
- `12\43` のログに `subs=4`（`g011`/`g010`）が出る。`g006` は `subs=12`
- `12\43` の非背景は **3,628**（この DAT はサブの位置が同一なので画素数は不変）
- `11\22` はサブ数 >1 の generator（`ppk0`=2, `ft00`=3, `g012`=2）を持つため **7,179 → 6,521** に増加（意図どおり）

### V-M15 サブの寿命・再初期化（AltanaView 準拠）

```powershell
$ROM='C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\11\17.DAT" lt_fire.png
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\11\18.DAT" lt_fire2.png
```
期待（実測値・2026-11-09）:
- `11\17` = **3,625 / 399**、`11\18` = **3,098 / 576**（折返し→頭打ちに変えたが不変）
- `11\22` = 6,521 / 247、`12\43` = 3,628 / 257
- ループフラグ（main のストリーム D `+0x8C` の op 0x05）は 4 つの DAT で**すべて false**
  → `--trace` で確認しても経過時間は単調増加（再初期化は起きない）

### V-M16 モデル従属モード（`+0x1a0 != 0` 相当）

```powershell
$ROM='C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\11\18.DAT" at_off.png          # 単体再生
.\x64\Release\EffectTest.exe --shot --frames 90 --bone 0  --motion idl0 "$ROM\11\18.DAT" at_b0.png
.\x64\Release\EffectTest.exe --shot --frames 90 --bone 10 --motion idl0 "$ROM\11\18.DAT" at_b10.png
```
期待（実測値・2026-11-09）:
- 単体再生 **3,098 / 576**（`--model` 無しは不変＝他 DAT のゴールデンも不変）
- `--bone 0` **7,898 / 686**、`--bone 10` **4,429 / 692**
- `--bone` 指定時は `--attached` が**自動 on** になり、VM#1 の位置 op が走らない
- 180 フレーム・`--bg 40F0F0`・`--bone 10` で「骨の位置に縦の柱状に集中」することを画像で確認
- `--bone-map` で分散を近似できる（`0:5,…,9:90` = 6,726 / 765、`0:0,…,9:40` = 6,267 / 765）
- **`--bone-map auto`**（体軸 Y で 0.05〜1.75 m の骨を等分に割当）= **8,513 / 748**、
  `cand=65` / `bones=94`。180 フレーム・`--bg 40F0F0` で「体の各所に炎」に最も近い

### V-M17 `0x1F`（ジオメトリ）— 既定は無効（`--geo`）

```powershell
$ROM='C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
.\x64\Release\EffectTest.exe --shot --frames 90 "$ROM\11\22.DAT" geo_off.png             # 既定（geo off）
.\x64\Release\EffectTest.exe --shot --frames 90 --geo on "$ROM\11\22.DAT" geo_on.png
.\x64\Release\EffectTest.exe --shot --frames 90 --geo on --geoflat on "$ROM\11\22.DAT" geo_flat.png
```
期待（実測値・2026-11-09）:

| 条件 | `11\17` | `11\22` | `12\43` |
|---|---|---|---|
| 既定（`--geo off`） | **3,625 / 399** | **6,521 / 247** | **3,628 / 257** |
| `--geo on` | 4,606 / 327 | **11,545 / 227** | 6,554 / 256 |
| `--geo on --geoflat on` | 4,507 / 327 | **6,796 / 263** | 2,435 / 223 |

- `--geo on` で `0x1F` の bbox/UV をインスタンスへ適用（ログに `geo=1 flat=0/1 half=(…) ctr=(…)`）
- `--geoflat on` は XZ 平面（Y=0）を**地面デカール**として世界軸で描く
- **既定は両方 off**（ゴールデンは off の値）。`0x1F` は妥当性ガード（0.01〜20 m）を通ったものだけ採用

## 3. 失敗時の切り分け

## 3. 失敗時の切り分け

### 3.0 既定サイズモードの変更（2026-11-09）

`--size` の**既定を `meter`（`0x21` のクワッド実寸。無ければ 1 m）に変更**した。
理由: VM が書く `scl` の X 成分が膨張し（`4.26` / `7.60` / `27.47`）、
**幅 4 m × 高さ 1 m の横長クワッド＝帯状アーティファクト**になっていたため
（`docs/ANALYSIS-AltanaView-Draw.md` §5.6）。

- 変更前（`scale`）: `11\17` 8,050 / 348、`11\18` 2,694 / 576、`11\22` 7,317 / 238、`12\43` 10,729 / 244
- **変更後（`meter`）**: `11\17` **3,625 / 399**、`11\18` **3,098 / 576**、`11\22` **6,521 / 247**、`12\43` **3,628 / 257**
- 帯（横長の黒塊）は解消。`--size scale` で従来値に戻せる


| 症状 | 疑う箇所 | 確認 |
|---|---|---|
| `[model] no mesh` | `0x2A` 読込（T-M03） | `--selftest` の `poly=` |
| 白一色 | テクスチャ（T-M02） | `tex=` と失敗件数のログ |
| 何も出ない | カメラ・AABB（T-M06/T-M07） | `GetBoneWorldAABB` のログ |
| エフェクトの数値が動く | `Gfx` の状態漏れ（T-M07） | V-M07 を先に実行 |

## 4. 記録テンプレート

| 実施日 | ビルド | V-M01 | V-M02 | V-M03 | V-M04 | V-M05 | V-M06 | V-M07 | V-M08 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|
| 2026-11-09 | Release x64 | ✅ | — | — | — | — | — | — | — | **P1a 完了**: `--skel` で bones=56 / motions=18、ルート変換 `mirrorY`（ModelTest の `matrixMirrorY`）で bone1 y=+1.151、エフェクト回帰 3,625/348 不変 |
