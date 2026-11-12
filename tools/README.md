# tools

- `EffectRes.ps1 <dat> [-Base <0/0.DAT>]` : エフェクト DAT の Schedule 命令列（`+0x50` 起点）と必要資源（`+0xAC` のリンク名 → `0x20`）を出力する、アプリ非依存の検証用オラクル。
  - 使い方: `.\tools\EffectRes.ps1 -Dat "C:\...\ROM\11\17.DAT"`
  - 最終行の `[res] SUMMARY ...` がゴールデン比較用（`docs/TEST-PLAN-Schedule.md` T-06、`docs/VERIFY-PLAN-Schedule.md` V-05）。
  - **スクリプトは ASCII のみで記述**する（PowerShell 5.1 は BOM 無し UTF-8 を ANSI として読むため、日本語リテラルは構文エラーになる）。
- `ShotStats.ps1 -Path <png> [-Step 4] [-Tolerance 3]` : `--shot` の PNG を間引き検査し、非背景点数と最大 RGB 合計を 1 行で出す。
  - 使い方: `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path out_fire.png`
  - 出力例: `path=out_fire.png size=1280x720 step=4 sampled=57600 nonBg=7950 maxSum=686 bg=23,15,13`
  - ゴールデン値は `docs/GOLDEN.md`（2026-11-12 に一本化）を参照。
  - こちらも **ASCII のみ**で記述する。
- `RingStats.ps1 -Path <png> [-Step 2] [-Tolerance 3]` : `--shot` の PNG が
  「中詰まり（直線・塊）」か「中抜け（円周・リング）」かを 1 行で出す。
  - 使い方: `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\RingStats.ps1 -Path out_fire.png`
  - 出力例: `path=out_fire.png bbox=289x301 aspect=0.96 fill=0.973 hollow=2.145 peak=0.304 rows=91`
  - `hollow` = 中央帯の平均輝度 / 外周帯の平均輝度。**< 1.0 で中抜け（リング状）**、> 1.0 で中詰まり。
  - `aspect` = 非背景の外接矩形の幅/高さ。`bbox` / `fill` / `peak`（中央行の輝度ピーク位置 0=中心 1=端）。
  - 円周配置のゴールデンは `docs/GOLDEN.md` §2.1、検証の経緯は
    `docs/superpowers/plans/2026-11-12-ring-arrangement.md`。
  - こちらも **ASCII のみ**で記述する。
