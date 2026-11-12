# ゴールデン値（単一の正）

最終更新: 2026-11-12（レビュー指摘の修正後）
計測環境: Windows 11 / MSVC v143 / Release x64 / D3D11（HARDWARE）/ 1280x720

> ゴールデンは**このファイルだけ**を正とします。他の文書（`docs/VERIFY-PLAN-*.md` /
> `docs/TEST-PLAN-*.md` / `docs/ANALYSIS-*.md`）に旧値が残っていても、判定は本ファイルで行います。

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
`--ring on` / `--ringradius xy` / `--ringscale 1`）

| DAT | `nonBg` | `maxSum` | 備考 |
|---|---|---|---|
| `11\17` | **5,310** | **115** | `--ring off` で 5,023 / 129 |
| `11\18` | **3,488** | **462** | `--ring off` で 3,053 / 465 |
| `11\22` | **962** | **298** | `0x06`/`0x1F` を持たないため不変 |
| `12\43` | **3,500** | **342** | `--ring off` で 3,615 / 244 |

条件の既定値に `--ring on`（`0x06`/`0x1F` の円周配置）を含む。

### 2.1 形状指標（`tools/RingStats.ps1`）

| DAT | bbox | aspect | fill | **hollow** | peak |
|---|---|---|---|---|---|
| `11\17` | 289x301 | 0.960 | 0.973 | **2.145** | 0.304 |
| `11\18` | 265x289 | 0.917 | 0.737 | **6.582** | 0.015 |
| `11\22` | 131x293 | 0.447 | 0.399 | **1.203** | 0.992 |
| `12\43` | 385x213 | 1.808 | 0.690 | **1.253** | 0.478 |

`hollow` = 中央帯の平均輝度 / 外周帯の平均輝度。**< 1.0 で中抜け（円周・リング状）**。
`--ring off` のとき `11\17`=2.851 / `11\18`=7.520 / `12\43`=1.333。

### 2.2 円周配置の検証（`--trace`）

生成直後（`elapsed < 0.06 s`）の粒子を XY 重心から測った半径:

| DAT / gen | op（引数の半径） | `--ring off` | `--ring on` |
|---|---|---|---|
| `11\17` `g002` | `0x1F` (0.2, 0.2) | r=0.0084 / CV 0.707 | **r=0.190 / CV 0.158** |
| `11\17` `g003` | `0x06` (0.2, 0.2) | r=0.236 / CV 0.350 | **r=0.196 / CV 0.130** |

半径が引数（0.2）と一致し、CV が下がる（＝円周上に整列）ことを確認。
詳細: `docs/superpowers/plans/2026-11-12-ring-arrangement.md`。

再現性: `srand(1)` 固定のため、同一ビルド・同一 DAT なら PNG は SHA-256 まで一致する。

## 3. フラグ別の基準（A/B 用）

| 条件 | 対象 DAT | `nonBg` / `maxSum` |
|---|---|---|
| `--colorfallback on` | `11\18` | 3,488 / 462 |
| `--colorfallback off` | `11\18` | （測定時に再取得。差は約 25 画素） |
| `--uvscroll off` | `12\6` | 992 / 464 |
| `--uvscroll on`（= `cell`。UV 矩形が全面のため同値） | `12\6` | 992 / 464 |
| `--ring off`（旧: 0x06=球面 / 0x1F=no-op） | `11\17` | 5,023 / 129（hollow 2.851） |
| `--ring off` | `11\18` | 3,053 / 465（hollow 7.520） |
| `--ringradius dist`（0x1F の半径に \|arg7\|） | `11\18` | 4,426 / 378（hollow **3.941**） |
| `--ringscale 2` | `11\17` | 6,380 / 148（hollow 1.477） |
| `--ringscale 4` | `11\17` | 8,665 / 191（hollow **0.954** = リング状 / aspect 1.553） |

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
| 2026-11-12 | **円周配置**（`0x06`/`0x1F`、`--ring` 既定 on）を反映して `--shot` を再基準化。形状指標（`RingStats.ps1` の aspect/fill/hollow）を追加 |
