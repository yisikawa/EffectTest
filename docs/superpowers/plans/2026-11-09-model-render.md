# 実装計画: NPC モデル描画（P1 / 2026-11-09）

> 設計: `docs/DESIGN-Model-Render.md`（承認待ち）
> 制約: **ModelTest は変更しない**（読み取りのみ）／EffectTest 内で完結

## 0. 前提

- ブランチ: `feature/schedule` から `feature/model-render` を分岐（親設計のルール: `feature/*` で小さく積む）
- 各タスクは単独でビルド・動作確認できる単位。**エフェクト描画のゴールデン（Fire 8,050/348 等）を壊さない**ことを毎回確認
- 検証はヘッドレス（`--model` / `--selftest`）＋画素（`tools\ShotStats.ps1`）で行う

## 1. タスク

| ID | 内容 | 成果物 | 受け入れ基準 |
|---|---|---|---|
| T-M01 | 数学・型の移植（依存ゼロ化） | `Model/DxMath.h`, `Model/ModelTypes.h` | ビルド成功。`fbxsdk.h` を include しない |
| T-M02 | `CData` / `CList` / テクスチャ読込の移植 | `Model/Model_Common.cpp` | `--selftest` で `0x20` が TIM として読める（既存 `Tim.cpp` と同一結果） |
| T-M03 | メッシュ読込（`0x2A`）の移植 | `Model/Model_Mesh.cpp` | `ROM\131\6.DAT` で `poly=` / `vertex=` が 0 でない（ログ） |
| T-M04 | ボーン（`0x29`）・モーション（`0x2B`）の移植 | `Model/Model_Anim.cpp`, `Model.cpp` | `bones=` > 0、`motion=` 一覧が `--action` の `0x2B` 名と一致 |
| T-M05 | `ModelView` ファサード | `ModelView.h/.cpp` | `Load(dat)` が成功し、`Info()` に骨/メッシュ/テクスチャ数が出る |
| T-M06 | スキン描画を Gfx に統合 | `GfxDrawSkinnedMesh()`（`Gfx.cpp` か `ModelGfx.cpp`） | 静止画で**非背景 > 0**、モデルが 1 体描ける |
| T-M07 | カメラ・状態の整合 | `App.cpp` | エフェクト描画のゴールデンが**不変**（V-08/V-13/V-14） |
| T-M08 | 選択連携とヘッドレス | `App.cpp`（`--model`、`[model] …` ログ） | `--model "ROM\131\6.DAT" --shot out_model.png` が成功、`[model] bones=… poly=… tex=…` |
| T-M09 | モーション再生 | `ModelView::Update(dt)`, `App.cpp` | `--model … --motion atk0 --frames 30` で骨行列が変化（ログ or 画素差） |
| T-M10 | 共存（P4 の土台） | `App.cpp` | モデル + エフェクトを同一フレームで描画し、両方が見える |

## 2. 検証コマンド（タスク完了ごと）

```powershell
$exe = (Resolve-Path '.\x64\Release\EffectTest.exe').Path
$ROM = 'C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'

# エフェクト回帰（不変であること）
Start-Process $exe -ArgumentList @('--shot', ('"'+$ROM+'\11\17.DAT"'), 'r_fire.png') -Wait
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path r_fire.png   # 8050 / 348

# モデル
Start-Process $exe -ArgumentList @('--model', ('"'+$ROM+'\131\6.DAT"'), '--shot', 'm_npc.png') -Wait
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ShotStats.ps1 -Path m_npc.png
```

## 3. ロールバック

- すべて**追加ファイル + `App.cpp`/`Gfx` の追加関数**で構成し、既存の `GfxDrawQuad` とエフェクト経路は書き換えない
- 問題時は `feature/model-render` を捨てれば `feature/schedule` の状態に戻る（＝エフェクト側のゴールデンは無傷）

## 4. 未確定（実装中に確定させる）

- `Render.cpp` のシェーダ（HLSL）がインライン文字列か外部ファイルか → 統合方法
- モデルの `0x20` が TIM 以外（DDS パレット等）を含む割合 → 失敗時の白 SRV 率をログに出して判断
- EffectTest のカメラ（`UpdateCamera`）と ModelTest の視野の差 → P1 の画素数ゴールデンは実装後に確定
