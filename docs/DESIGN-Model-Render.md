# NPC モデル描画（P1）設計書（2026-11-09）

> 親設計: `docs/DESIGN-List-To-Screen.md` §5 **P1: NPC モデルを表示**
> 本書は P1 を **EffectTest 内で完結**させるための設計。実装は未着手（ルール: 設計 → 計画 → テスト → 検証 → 明示指示後に実装）。

## 0. 絶対制約

- **ModelTest（`D:\projects\ModelTest`）は一切変更しない**。参照（読み取り）のみ。
- 移植は EffectTest 側へ**複製**して行い、ModelTest へのファイル参照・サブモジュール化・共有ライブラリ化はしない。
- 移植元は各ファイル冒頭コメントに「移植元: ModelTest/<file> (参照のみ)」と記録する。

## 1. 目的・スコープ

> **重要（2026-11-09 調査追加）**: エフェクト DAT は骨・メッシュ・モーションを持たず（`docs/ANALYSIS-Model-For-Effects.md`）、
> エフェクト単体の再生にモデルは不要。参照動画（モデルに当てた記録）を再現するには**骨のワールド行列だけ**必要で、**メッシュは不要**。
> そのため P1 を **P1a（骨＋モーションのみ / 0.5〜1 日）** と **P1b（メッシュ描画 / 1.5〜2 日）** に分割する。

- List で NPC を**単一選択**したとき、その DAT（`0x2A` メッシュ / `0x29` ボーン / `0x2B` モーション / `0x20` テクスチャ）を読み、
  **モデルを画面に表示**する（親設計 P1 受け入れ基準）。
- 同じ選択で **Schedule リストが埋まる**（既存機能）／`0x2B` のモーション名で**モデルが動く**（親設計 P2 の前提）。
- エフェクト描画（P3、既存）と**同一フレームに共存**できる土台を作る（親設計 P4）。

**非対象（本書では扱わない）**
- PC パーツ合成・インベントリ（`CPC::LoadPCParts` / `LoadInventory`）
- FBX 出力（`SaveFile.cpp` 一式）、4 面図、影（`ShadowRendering`）、マウス操作 UI
- ボーン追従エフェクト（親設計 P4 の任意項目）
- エフェクトの見た目チューニング（`docs/VERIFY-PLAN-Schedule.md` V-15/V-16 の残差）

## 2. 参照元の実測（2026-11-09 / 読み取りのみ）

| ファイル | 行数 | 役割 | 移植 |
|---|---|---|---|
| `Model.cpp` | 1,543 | CModel（読込・変換・トランスフォーム） | する |
| `Model_Mesh.cpp` | 1,216 | メッシュ（`0x2A`）読込・FVF 変換 | する |
| `Model_Anim.cpp` | 729 | ボーン（`0x29`）・モーション（`0x2B`）読込 | する |
| `Model_Common.cpp` | 933 | `CData` / `CList` / テクスチャ読込 | する（PC 依存部は除外） |
| `Model.h` | 646 | 上記の宣言（`fbxsdk.h` を include しているだけ） | する（FBX include 削除） |
| `Render.cpp` | 439 | **D3D11** でモデル描画・ライト・影 | 描画部のみ統合 |
| `Dx.cpp` | 720 | D3D11 デバイス生成 | しない（EffectTest の `Gfx` を使う） |
| `DxMath.h` | 260 | D3DX 数学の代替（自前） | する |
| `SaveFile.cpp` | 2,302 | FBX 出力 | **しない** |
| `WinMain.cpp` | 1,549 | UI・操作 | しない（EffectTest の `App.cpp` を使う） |

**重要な実測**
- `Model*.cpp` / `Render.cpp` / `Dx.cpp` に **`fbxsdk` / `FbxManager` の参照は 0 件**。FBX SDK は `SaveFile.cpp` 専用。
- `Render.cpp` は既に **`ID3D11Device` 系**で描画している（D3D9 ではない）。
- 行列・ベクトルは `DxMath.h`（自前 `D3DXMATRIX` 等）で完結。

## 3. アーキテクチャ（EffectTest 側）

```
App.cpp                      … 選択 → ModelView へ DAT を渡す／毎フレーム Update・Draw
ModelView.h/.cpp（新規）      … EffectTest 向けファサード（Load/Update/Draw/Info）
  ├─ Model/Model.h           … 移植: CModel / CMesh / CBone / CMotionFrame / CData
  ├─ Model/Model*.cpp        … 移植: 読込・変換・モーション
  ├─ Model/DxMath.h          … 移植: 行列・ベクトル
  └─ Gfx（既存を拡張）        … スキンメッシュ用 VS/PS・入力レイアウト・状態
```

- **デバイスは共有**: `GfxDevice()` / `GfxContext()` を `ModelView` に渡す（`Dx.cpp` は移植しない）。
- **カメラは共有**: `GfxSetCamera(view, proj)` と同じ行列を `ModelView` に渡す。
- **合成順序**（親設計 P4）: 1) モデル = 通常アルファ・**Z 書き込み ON** → 2) エフェクト = アルファ・**Z 書き込み OFF**（既存 `GfxDrawQuad` の状態）。
- **テクスチャ**: モデルの `0x20` は TIM/DXT（`Tim.cpp` の既存デコーダを再利用）。DDS 等の外部ファイル参照が混在する場合は無視して白で代替。
- **時間**: 既存の `dt`（`App.cpp`）を `ModelView::Update(dt)` に渡す。60 fps 前提は親設計 §1 に従う。

**Gfx への追加（最小）**
```cpp
// スキンメッシュ 1 回分の描画（頂点/インデックス/テクスチャ/骨行列を受け取る）
bool GfxDrawSkinnedMesh(const void* vb, size_t vbBytes, const void* ib, size_t ibBytes,
                        int indexCount, const float (*boneMatrices)[16], int boneCount,
                        ID3D11ShaderResourceView* srv);
```
- 実装は `Render.cpp` のシェーダ（HLSL）と入力レイアウトを移植し、EffectTest の cbuffer 設計に合わせる。
- 既存の `GfxDrawQuad`（ビルボード）には**一切触れない**（回帰防止）。

## 4. データ仕様（親設計 §0 の用語に対応）

| 種別 | 内容 | 既存実装 |
|---|---|---|
| `0x2A` メッシュ | `DAT2AHeader`（64 B）+ ポリゴン/ボーン表/ウェイト/頂点 | なし（移植） |
| `0x29` ボーン | 親 index・初期行列・名前（4 文字） | なし（移植） |
| `0x2B` モーション | ボーン別キーフレーム（回転/移動/スケール） | なし（移植） |
| `0x20` テクスチャ | TIM/DXT | **あり**（`Tim.cpp`） |
| `0x07` Schedule | 既存（列挙・選択・Effect 再生） | あり |

検証対象の代表 DAT: `ROM\131\6.DAT`（NPC ROM1・39 Schedule・`items=1266`）。

## 5. 検証方針（詳細は TEST/VERIFY 計画）

1. **ヘッドレス**: `--model "<dat>" --shot out_model.png` を追加（GPU 必須）
   - 骨数・メッシュ数・ポリゴン数・テクスチャ数をログ（`[model] …`）
2. **`--selftest` 拡張**: `model: gen=… mesh=… poly=… bones=… tex=… motion=…`
3. **画素**: 非背景点数 > 0（4px 間引き）、`--bg RRGGBB` で背景既知
4. **回帰**: 既存の V-01〜V-16（エフェクト側）が**すべて不変**であること
5. **共存（P4）**: モデル + エフェクトを同時描画した 1 枚で、両方が見えること

## 6. リスクと対策

| リスク | 対策 |
|---|---|
| `Render.cpp` のグローバル状態（`g_mView` 等）と衝突 | `ModelView` 内に閉じ込め、`App.cpp` からは行列を渡すだけにする |
| EffectTest の `Gfx` と深度/ブレンド状態が干渉 | モデル描画後に**必ず**エフェクト用状態へ戻す（`GfxBeginFrame` の状態を再設定） |
| DXT 以外（パレット/DDS）テクスチャ | 失敗時は白 SRV で継続（`tex=-1` と同じ扱い） |
| 30 万ポリゴン級 DAT での速度 | 1 体のみ・60 fps 維持を確認。超える場合は LOD（`PolyLod*`）を使う |
| ModelTest との**二重管理**（同じバグを 2 箇所で直す） | 本書の移植は「参照実装の写し」と明記し、修正は EffectTest 側のみ |

## 7. 見積り（設計書 §5 の 0.5 日を修正）

| 工程 | 見積り |
|---|---|
| **P1a: 骨＋モーションのみ**（`0x29`+`0x2B` 読込・ワールド行列更新・ログ） | **0.5〜1.0 日** |
| P1b: 移植（Model/Common/Mesh + DxMath） | 1.0 日 |
| P1b: Gfx 統合（スキン描画・状態・カメラ） | 0.5 日 |
| P1b: App 連携（選択→表示、`--model`、ログ） | 0.5 日 |
| 検証（ヘッドレス＋画素＋回帰） | 0.5 日 |
| **合計** | **P1a 0.5〜1.0 日 / P1a+P1b 約 2.5〜3 日** |

> 参照動画の再現だけが目的なら **P1a のみ**で足りる（メッシュは不要）。
> ただしエフェクト側の寸法・位置が未解決（V-16）なので、骨を入れても見た目は参照に近づかない。

## 8. 承認が必要な点

- 本書のスコープ（非対象の切り方）で良いか
- `GfxDrawSkinnedMesh` を `Gfx` に足す方針で良いか（別ファイル `ModelGfx.cpp` に分離する案もある）
- 実装に着手して良いか（ルール上、明示指示後）
