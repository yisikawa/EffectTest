# ゴールデン値（単一の正）

最終更新: 2026-11-12（レビュー指摘の修正後）
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
| `11\17` | **9,310** | **166** | `--arrange line` で 5,023 / 129 |
| `11\18` | **6,257** | **353** | `--arrange line` で 3,053 / 465 |
| `11\22` | **962** | **298** | `0x06`/`0x1F` を持たないため不変 |
| `12\43` | **4,780** | **263** | `--arrange line` で 3,615 / 244 |

条件の既定値に `--arrange ring`（`0x06`/`0x1F` の円周上配置）+ `--ringplane xy` + `--ringscale 4` を含む。

### 2.1 形状指標（`tools/RingStats.ps1`）

| DAT | bbox | aspect | fill | **hollow** | peak |
|---|---|---|---|---|---|
| `11\17` | 503x323 | 1.557 | 0.913 | **0.774** | 0.449 |
| `11\18` | 423x333 | 1.270 | 0.710 | **1.821** | 0.411 |
| `11\22` | 131x293 | 0.447 | 0.399 | **1.206** | 0.992 |
| `12\43` | 387x335 | 1.155 | 0.586 | **1.239** | 0.646 |

`hollow` = 中央帯の平均輝度 / 外周帯の平均輝度。**< 1.0 で中抜け（円周・リング状）**。
`--arrange line` のとき `11\17`=2.851 / `11\18`=7.520 / `12\43`=1.333。

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
