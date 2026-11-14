# ffxitool5 のアルファ処理（実測）と、当方のアルファクリップ実装 — 2026-11-14

対象: `D:\tools\ffxitool13032900\ffxitool5.exe`（SHA-256 `2b159dc0…f69` / D3D8 / 固定ベース 0x400000）

---

## 0. 結論

1. **ffxitool5 はアルファテスト（クリップ）を使っていない。** 全 `SetRenderState` 呼び出しを
   確認した限り、**ブレンドのみ**（`SRCBLEND/DESTBLEND/ALPHABLENDENABLE/BLENDOP`）。
   `ALPHATESTENABLE(15)` / `ALPHAREF(24)` / `ALPHAFUNC(25)` は**一度も出てこない**。
2. D3D8 で「クリップ」をやるなら `ALPHATESTENABLE` + `ALPHAFUNC` + `ALPHAREF`。
   D3D11 には固定機能のアルファテストが無いため、**ピクセルシェーダの `discard`** が等価。
   これを `--alphaclip` として実装した（既定 off）。
3. **FFXI の加算系エフェクト（Cure 等）は α が 0.06〜0.5 に分布**するため、
   `ref=128`(0.5) では**全消滅**する。クリップを使うときの実用レンジは **16〜64**。

---

## 1. 根拠（レンダーステートの実測）

`SetTransform` = vtable `+0x94`、`SetRenderState` = `+0xC8`（`0x100`=D3DTS_WORLD の実測から確定）。

| 呼び出し元 | 機械語（実測） | 解釈 |
|---|---|---|
| `FUN_004050b0` @0x40512e | `6A 01` / `68 AB000000` | `SetRenderState(0xAB=BLENDOP, 1=ADD)` |
| `FUN_004050b0` @0x405154 | `6A 02` / `6A 14` | `SetRenderState(0x14=DESTBLEND, 2=ONE)` |
| `FUN_00405610` | `(0x1B,1) (0x13,5) (0x14,2) (0xE,0) (7,0)` | `ALPHABLENDENABLE=TRUE / SRCBLEND=SRCALPHA / DESTBLEND=ONE / ZWRITE=0 / ZENABLE=0` |
| `FUN_00405ab0`（3D パス） | `(0x13,5) (0x14,6) (0x16,1) (0x89,0)` | `SRCALPHA / INVSRCALPHA（通常α） / CULLNONE / LIGHTING=OFF` |
| `FUN_00415531` @0x415559 | `6A 00` / `6A 07` | `SetRenderState(7=ZENABLE, 0=FALSE)` |

- **状態 15 / 24 / 25（アルファテスト系）は 0 回**。
- したがって ffxitool5 の透過は **ブレンド**で、深度テストも切っている
  （ZENABLE=0 / ZWRITE=0）ため、加算合成の重なり順に依存しない。

### 1.1 D3D8 → D3D11 の対応

| D3D8 | 意味 | 当方（D3D11） |
|---|---|---|
| `ALPHATESTENABLE=TRUE` | アルファテスト有効 | PS の `discard` |
| `ALPHAFUNC=GREATER` | `srcA > ref` を残す | `if (a < ref) discard;`（≧ で残す） |
| `ALPHAREF=n` (0..255) | しきい値 | `--alpharef n` |
| `ALPHABLENDENABLE=FALSE` | ブレンドしない | `--blend none`（`GFX_BLEND_NONE`） |

---

## 2. 実装

- `Gfx`：
  - `CBData.opt.y = クリップ有効 / opt.z = ref/255`
  - PS：`if (gOpt.y > 0.5 && t.a * i.col.a < gOpt.z) discard;`
  - `GFX_BLEND_NONE`（`BlendEnable=FALSE`）を追加
  - `GfxSetAlphaClip(on, ref255)` / `GfxAlphaClip()` / `GfxAlphaRef()`
- `App`：`--alphaclip on|off`、`--alpharef 0..255`、`--blend none`
- **既定 off**。指定しない限り従来のブレンドで、ゴールデンは不変
  （`tools/Verify-All.ps1` が ALL PASS であることを確認）。

---

## 3. 実測（A/B）

### 3.1 `11\17`（`--frames 90` / step=4）

| 指定 | nonBg | maxSum | 所感 |
|---|---|---|---|
| 既定（alpha_add） | 10,343 | 204 | 従来 |
| `--alphaclip on`（ref 128） | **613** | **645** | 芯だけ残り、硬く明るい |
| `--alphaclip on --blend none` | 613 | 645 | 加算と一致（背景が暗いため） |
| `--blend none` のみ | 10,345 | 465 | 板全体が不透明（α=0 の角も出る） |
| `--alphaclip on --alpharef 16` | 10,343 | 204 | クリップ無効と同じ（α が全て 16 以上） |

### 3.2 Cure `tgt0`（`--frames 260 --sched tgt0`）

| 指定 | nonBg | maxSum |
|---|---|---|
| 既定 | 6,991 | 313 |
| `--alphaclip on`（ref 128） | **0** | 51（＝全消滅） |
| `--alphaclip on --alpharef 64` | 5,980 | 289 |
| `--alphaclip on --alpharef 16` | 6,991 | 313（変化なし） |

→ **Cure の粒子 α は 0.06〜0.5**。`ref=128` は「不透明度の足切り」としては強すぎる。
FFXI の加算エフェクトの α は「形（カバレッジ）」ではなく「強度」なので、
クリップは **マスク付きスプライト**に限って使い、加算グローには使わない。

---

## 4. 使い分け（推奨）

| 用途 | 指定 |
|---|---|
| ffxitool5 / AltanaView と同じ見た目 | **既定**（`alpha_add` ブレンド） |
| 輪郭を硬くしたい（マスク付きスプライト） | `--alphaclip on --alpharef 64 --blend none` |
| 板の形を確認したい（デバッグ） | `--blend none`（クリップ無しで全板を出す） |

---

## 5. 未確定 / 今後

- `ALPHAREF` の「正解値」は ffxitool5 に存在しない（アルファテスト未使用）。
  データ依存なので、テクスチャの黒割合（`blackRatio`）から自動決定する案は未実装。
- MRT/ステンシルを使う方式（深度プリパス＋クリップ）は未検討。
