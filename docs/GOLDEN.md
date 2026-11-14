# ゴールデン値（単一の正）

最終更新: 2026-11-14（`--nodetypes` 追加。11\17 / 11\18 / 11\22 を再測）
計測環境: Windows 11 / MSVC v143 / Release x64 / D3D11（HARDWARE）/ 1280x720

> ゴールデンは**このファイルだけ**を正とします。他の文書（`docs/VERIFY-PLAN-*.md` /
> `docs/TEST-PLAN-*.md` / `docs/ANALYSIS-*.md`）に旧値が残っていても、判定は本ファイルで行います。
> コード上で何ができるようになったかは `docs/CAPABILITIES-2026-11-12.md` を参照。

---

## 1. `--selftest`（GPU 不要）

```powershell
.\x64\Release\EffectTest.exe --selftest "$ROM\11\17.DAT"
Start-Sleep -Milliseconds 300
Get-Content effecttest_selftest.txt
```

| DAT | `chunks` | `generators` | `keys`（ops/resolved/unresolved） | `texnames`=`texspecific` | `rebuild` | `missing` |
|---|---|---|---|---|---|---|
| `11\17` | 30 | 8 | **8 / 8 / 0** | 5 = 5 | ok | 3 |
| `11\18` | 38 | 11 | **9 / 9 / 0** | 7 = 7 | ok | 2 |
| `11\22` | 28 | 14 | **33 / 33 / 0** | 6 = 6 | ok | 2 |
| `12\43` | 32 | 12 | **10 / 10 / 0** | 6 = 6 | ok | 0 |

判定:
- **`keys` の `unresolved` は 0**（VM#1 のキー op の第 3 dword が 0x19 チャンク名として全件解決）。
- **`texnames == texspecific`**（0x20 の数と specific 名の数が一致）。
- **`rebuild = ok`**（同一 `EffectContext` で 2 回 `Build` しても索引が累積しない）。
- `missing` はタイムラインが参照した generator 名の未解決数（現状は `[spawn] generator not found:` を
  ログに出すだけ。ベース DAT へのフォールバックは未実装）。

主要インスタンスの値（`frame=59`、α と Scale の健全性チェック）:

| DAT / generator | 期待（2026-11-12 以降） | 修正前 |
|---|---|---|
| `11\18` `g003` | `col=(0.86,0.44,0.24,0.58)` | `col α=0.00` |
| `11\17` `g000` | `scl=(0.800,0.705,1.000)` | `scl=(0.000,0.000,1.000)` |
| `11\17` `g005` | `scl=(1.660,1.660,1.000) col α=0.49` | `scl=0 / α=0.00` |
| `12\43` 全 generator | `col α=0.50` | `col α=0.00` |

## 2. `--shot`（GPU 必須）

```powershell
.\x64\Release\EffectTest.exe --shot "$ROM\11\17.DAT" out.png --frames 90
tools\ShotStats.ps1 -Path out.png
```

条件: **1280x720 / step=4 / 57,600 サンプル / `--frames 90` / 既定**
（`--layout off` / `--emit on` / `--geo off` / `--uvscroll off` / `--colorfallback on` / `--size meter` /
`--arrange ring` / `--ringplane xy` / `--ringscale 4` / `--ringradius xy`）

| DAT | `nonBg` | `maxSum` | 備考 |
|---|---|---|---|
| `11\17` | **10,343** | **204** | `--arrange line`（0x1F 無効）で 5,023 / 129 |
| `11\18` | **3,948** | **454** | `--arrange line` で 3,053 / 465 |
| `11\22` | **970** | **303** | `--arrange line` で 962 / 298 |
| `12\43` | **4,661** | **263** | `--arrange line` で 3,615 / 244 |

> **2026-11-14 再測**: `--nodetypes` 追加の前後（同一条件・`srand(1)`）で値は同一。
> 上の表は 11\17=9,313 / 169、11\18=3,583 / 449、11\22=962 / 303 に更新した
> （旧値 9,310 / 171・3,588 / 449・962 / 302 は 2026-11-12 時点の記録）。

> **2026-11-14 追記（3D 座標の修正）**: VM の引数読み出しを命令長で制限した（`EffectVm.cpp`）。
> 修正前は `len=2` の命令で次命令の引数を拾い、`Pos.z` が -0.5/フレームで流れていた（kr10 が z=-41 m）。
> 上の表はこの修正後の再測値。詳細: `docs/ANALYSIS-Cure-tgt0-2026-11-14.md` §8。

条件の既定値に `--arrange ring`（`0x06`=円周 / `0x1F`=円錐散布）+ `--ringplane xy` + `--ringscale 4` を含む。

### 2.1 形状指標（`tools/RingStats.ps1`）

| DAT | bbox | aspect | fill | **hollow** | peak |
|---|---|---|---|---|---|
| `11\17` | 503x323 | 1.557 | 0.913 | **0.885** | 0.330 |
| `11\18` | 287x331 | 0.867 | 0.599 | **3.119** | 0.021 |
| `11\22` | 147x295 | 0.498 | 0.351 | **1.210** | 0.993 |
| `12\43` | 387x335 | 1.155 | 0.584 | **1.228** | 0.646 |

`hollow` = 中央帯の平均輝度 / 外周帯の平均輝度。**< 1.0 で中抜け**。
`--arrange line` のとき `11\17`=2.851 / `11\18`=7.520 / `11\22`=1.206 / `12\43`=1.333。

> `0x1F` は 2026-11-12 の逆コンパイル（`FUN_0040e650` case 0x1F）で
> **方向ベクトル `arg2..4` を 2 角度で回す円錐散布**と判明。以前の「円周半径 = arg0/arg1」実装は誤りで差し替えた。

画像（`--frames 90`、既定）:

| 配置 | 画像 |
|---|---|
| 旧（直線） | `docs/shots/2026-11-12-line-11_17.png` |
| 円周（既定） | `docs/shots/2026-11-12-ring-11_17.png` |
| らせん（`--arrange spiral --spiralturns 2`） | `docs/shots/2026-11-12-spiral-11_17.png` |

### 2.2 円周配置の検証（`--trace`）

生成直後（`elapsed < 0.06 s`）の粒子を XY 重心から測った半径:

| DAT / gen | op（引数の半径） | `--arrange line` | `--arrange ring` |
|---|---|---|---|
| `11\17` `g002` | `0x1F` (0.2, 0.2) | r=0.0084 / CV 0.707 | **r=0.190 / CV 0.158** |
| `11\17` `g003` | `0x06` (0.2, 0.2) | r=0.236 / CV 0.350 | **r=0.196 / CV 0.130** |

半径が引数（0.2）×倍率と一致し、CV が下がる（＝円周上に整列）ことを確認。
位相は**粒子番号の黄金角**で均等に割り付ける（乱数位相だとムラが出て線に見える）。
詳細: `docs/superpowers/plans/2026-11-12-ring-arrangement.md`。

### 2.3 参照動画（`frames/v_*.png`）との比較

参照は **AltanaView で `11/18` を NPC に付けて再生**したもの（960×540、3D ビュー = x282..907 / y2..514、背景 `RGB(64,240,240)`）。
カメラ実値は `AltanaView.ini` に残っている: **Translation (0.008, 0.985, 2.605) = 距離 2.785**、回転はクォータニオン (0.595, -0.081, 0.797, 0.061)。

| 指標 | 参照 `v_0062` | 我々（既定 dist 14） | 我々（`--dist 2.8 --emitrate 60 --scale 0.15`） |
|---|---|---|---|
| bbox | 610×511 / 625×512（97.6%×99.8%） | 387×361（30%×50%） | 1049×719 |
| aspect | 1.194 | 1.072 | 1.459 |
| **fill** | **0.323** | 0.691 | **0.350** ✓ |
| **hollow** | **0.670** | 2.042 | 0.898 ✗ |

- **カメラ距離が最大の差**（14 vs 2.785＝5 倍遠い）。`--dist` で一致させると bbox が画面全体になる。
- **粒子サイズが過大**。`--scale 0.15`＋`--emitrate 60` で **fill が参照の帯（0.35 vs 0.32）**に入る。
- `hollow` が残るのは、参照の中央が**キャラクター（暗い）**で、我々はモデル無しのため。
  `--attached on` は 2026-11-12 に修正済み（実測 `FUN_0040e650` は行列コピーだけを条件分岐し、
  **VM を停止しない**）。修正前はモデル指定で効果が消えた（bbox 177×503 → 1049×719）。
- 画面アスペクト（1280×720 = 1.78 vs 625×512 = 1.22）も bbox/aspect 比較に効く。

再現コマンド:

```powershell
.\x64\Release\EffectTest.exe --shot "$ROM\11\18.DAT" out.png --frames 90 `
  --dist 2.8 --scale 0.15 --emitrate 60 --bg 40F0F0
tools\RingStats.ps1 -Path out.png
```

再現性: `srand(1)` 固定のため、同一ビルド・同一 DAT なら PNG は SHA-256 まで一致する。


## 3. フラグ別の基準（A/B 用）

| 条件 | 対象 DAT | `nonBg` / `maxSum` |
|---|---|---|
| `--colorfallback on` | `11\18` | 6,257 / 353 |
| `--colorfallback off` | `11\18` | 6,254 / 353（差は約 3 画素） |
| `--uvscroll off` | `12\6` | 992 / 464 |
| `--uvscroll on`（= `cell`。UV 矩形が全面のため同値） | `12\6` | 992 / 464 |
| `--arrange line`（旧: 0x06=球面 / 0x1F=no-op） | `11\17` | 5,023 / 129（hollow 2.851） |
| `--arrange line` | `11\18` | 3,053 / 465（hollow 7.520） |
| `--ringscale 1`（データ半径のまま） | `11\17` | 5,587 / 105（hollow 1.563） |
| `--ringscale 2` | `11\17` | 6,755 / 130（hollow 1.011） |
| `--ringscale 4`（既定） | `11\17` | 9,310 / 166（hollow **0.774** = リング状） |
| `--ringscale 6` | `11\17` | 11,816 / 147（hollow 0.725） |
| `--ringplane xz`（scale 1） | `11\17` | 5,467 / 108（hollow 1.550） |
| `--arrange spiral --spiralturns 2` | `11\17` | 10,523 / 144（hollow 0.774 / aspect 1.521） |
| `--arrange spiral --spiralturns 2` | `12\43` | 5,320 / 329（hollow 1.277 / aspect 0.975） |
| `--ringradius dist`（scale 1） | `11\18` | 4,661 / 367（hollow 3.914） |

> `--uvscroll` は `off` と `on` で **PNG のハッシュが変わる**ことを確認済み（`12\6` / `11\18` / `11\6`）。
> `on` と `cell` が一致するのは `0x21` の UV 矩形が全面（`uw = 1`）で式が同値になるため。
> **`0x21` フレームと VM#2 `0x27/0x28` を併せ持つ generator は `11\0-11\19` / `12\0-12\19` に存在しない**
> ため、両者が分岐する実データは未発見。

## 4. 広域回帰

| 範囲 | 判定 |
|---|---|
| `11\0`–`11\19` / `12\0`–`12\19` の `--selftest` | **40/40 成功**（終了コード 0・`LOAD FAILED` なし・`rebuild = ok`） |

## 5. 更新履歴

| 日付 | 内容 |
|---|---|
| 2026-11-12 | 初版。レビュー（`docs/REVIEW-2026-11-10-Issues-And-Fixes.md`）の修正に伴い全面再基準化。`keys` / `texnames` / `rebuild` / `missing` を新規ゴールデンに追加 |
| 2026-11-12 | **円周配置**（`0x06`/`0x1F`）を反映して `--shot` を再基準化。形状指標（`RingStats.ps1` の aspect/fill/hollow）を追加 |
| 2026-11-12 | 配置を**粒子番号の黄金角で均等割り**に変更（乱数だとムラが出て線に見える）。`--arrange line\|ring\|spiral`、`--ringplane xy\|xz\|yz`、`--spiralturns` を追加し、既定を `ring` + `ringscale 4` に。`docs/shots/` に比較画像（line/ring/spiral）を追加 |

---

## 3. Cure `tgt0`（入れ子 Schedule / 2026-11-14 追加）

```powershell
.\x64\Release\EffectTest.exe --shot "$ROM\10\9.DAT" tgt0.png --frames 260 --sched tgt0
tools\ShotStats.ps1 -Path tgt0.png
```

| 項目 | 期待値 |
|---|---|
| `nonBg` / `maxSum` | **2,488 / 322** |
| ログ | `[build] sched=tgt0` |
| ログ | `[sub] mdam: base insts=3 at frame=200 ops=01 2B 00` |
| 解消 | `[spawn] generator not found: mdam` が出ない（11\17 も同様） |

`mdam` は **ベース DAT（`ROM\0\0.DAT`）の 0x07**（offset 0x81780 / 128 B / `01 2B 00`）。
旧実装はローカル DAT しか探さず、`mdam` を「未解決 generator」として捨てていた。
詳細: `docs/ANALYSIS-Cure-tgt0-2026-11-14.md`


---

## 4. カメラ座標系（`--camera ffxi` / 2026-11-14 追加）

ffxitool5（`FUN_00405ab0`）の View/Projection を再現するモード。**既定は orbit のまま**。

| 項目 | 期待値 |
|---|---|
| ログ | `[cam] mode=ffxi rh eye=(0,0.05,-5) at=(0,0,0) up=(0,1,0) fov=45 aspect=4:3 zn=1 zf=20 (right=-X up=+Y fwd=+Z)` |
| `--shot ROM\10\9.DAT --sched tgt0 --frames 40 --res 640x480 --camera ffxi` | **nonBg=6,433 / maxSum=347** |
| 同位置の orbit（`--yaw 0 --pitch 0.01 --dist 5`） | 6,424 / 346 |
| 画像の関係 | 左右反転で平均差 **2.378**（反転なし 11.778 / step=2） |

詳細: `docs/ANALYSIS-ffxitool5-Camera-2026-11-14.md`

