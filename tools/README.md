# tools

- `EffectRes.ps1 <dat> [-Base <0/0.DAT>]` : エフェクト DAT の Schedule 命令列（`+0x50` 起点）と必要資源（`+0xAC` のリンク名 → `0x20`）を出力する、アプリ非依存の検証用オラクル。
  - 使い方: `.\tools\EffectRes.ps1 -Dat "C:\...\ROM\11\17.DAT"`
  - 最終行の `[res] SUMMARY ...` がゴールデン比較用（`docs/TEST-PLAN-Schedule.md` T-06、`docs/VERIFY-PLAN-Schedule.md` V-05）。
  - **スクリプトは ASCII のみで記述**する（PowerShell 5.1 は BOM 無し UTF-8 を ANSI として読むため、日本語リテラルは構文エラーになる）。
- `ShotStats.ps1 -Path <png> [-Step 4] [-Tolerance 3]` : `--shot` の PNG を間引き検査し、非背景点数と最大 RGB 合計を 1 行で出す。
  - 使い方: `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path out_fire.png`
  - 出力例: `path=out_fire.png size=1280x720 step=4 sampled=57600 nonBg=7950 maxSum=686 bg=23,15,13`
  - ゴールデン値は `docs/TEST-PLAN-Schedule.md` T-09/T-14、`docs/VERIFY-PLAN-Schedule.md` V-08/V-13 を参照。
  - こちらも **ASCII のみ**で記述する。
