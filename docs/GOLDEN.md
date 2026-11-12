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
（`--layout off` / `--emit on` / `--geo off` / `--uvscroll off` / `--colorfallback on` / `--size meter`）

| DAT | `nonBg` | `maxSum` | 備考 |
|---|---|---|---|
| `11\17` | **5,023** | **129** | 修正前 5,023 / 105 |
| `11\18` | **3,053** | **465** | 修正前 3,236 / 477 |
| `11\22` | **962** | **298** | 修正前 962 / 279 |
| `12\43` | **3,615** | **244** | 修正前 3,626 / 245 |

再現性: `srand(1)` 固定のため、同一ビルド・同一 DAT なら PNG は SHA-256 まで一致する。

## 3. フラグ別の基準（A/B 用）

| 条件 | 対象 DAT | `nonBg` / `maxSum` |
|---|---|---|
| `--colorfallback on` | `11\18` | 3,053 / 465 |
| `--colorfallback off` | `11\18` | 3,028 / 465 |
| `--uvscroll off` | `12\6` | 992 / 464 |
| `--uvscroll on`（= `cell`。UV 矩形が全面のため同値） | `12\6` | 992 / 464 |

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
