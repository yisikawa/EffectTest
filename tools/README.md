# tools

- `EffectRes.ps1 <dat> [-Base <0/0.DAT>]` : エフェクト DAT の Schedule 命令列（`+0x50` 起点）と必要資源（`+0xAC` のリンク名 → `0x20`）を出力する、アプリ非依存の検証用オラクル。
  - 使い方: `.\tools\EffectRes.ps1 -Dat "C:\...\ROM\11\17.DAT"`
  - 最終行の `[res] SUMMARY ...` がゴールデン比較用（`docs/TEST-PLAN-Schedule.md` T-06、`docs/VERIFY-PLAN-Schedule.md` V-05）。
  - **スクリプトは ASCII のみで記述**する（PowerShell 5.1 は BOM 無し UTF-8 を ANSI として読むため、日本語リテラルは構文エラーになる）。
