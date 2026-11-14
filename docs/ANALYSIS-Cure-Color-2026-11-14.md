# Cure の色の反映方法 — ffxitool5 実測と参照画像との比較（2026-11-14）

対象: `ffxitool5.exe`（SHA-256 `2b159dc0…f69`）と当方 `--colorop` 実装
参照: `docs/shots/2026-11-13-cure-av-1500ms.png`（AltanaView / Cure 1.5 s）

---

## 0. 結論

1. **ffxitool5 の色式は `MODULATE4X`**：テクスチャ × 頂点色 × **4**（RGB も α も）。
   当方は RGB×1.5 / α×1 だったため、**α の ×4 が欠落**していた（＝暗く淡い）。
   これを `--colorop ffxi`（RGB×4, α×4）として実装した。
2. **画面同士の画素比較は ffxitool5 では不可能**。README の操作（NumPad `-` で Cure へ切替、
   Space で再生）は実測で確認できたが、**3D ビューはウィンドウサイズに依らず幅 8 px 固定**
   （624x441 でも最大化 2560x1377 でも同じ。`PrintWindow` でもログのみ）。
   代わりに **AltanaView の参照画像**と色を定量比較した。
3. 比較の結果、**明るさの差は解消**（参照 96/77/102 に対し 80/77/156）。
   残差は **青偏重（B/R 1.95 対 1.06）**で、原因は**テクスチャステージではなく頂点色（VM）**。
   白テクスチャで測ると当方の頂点色は **(0.23, 0.23, 0.50)** と **R=G** の不自然な一致を示す。

---

## 1. 画面比較の可否（実測）

### 1.1 README の操作方法（実測で確認）

README（`D:\tools\ffxitool13032900\readme.txt`）の FFXITool5 の項:

```
NumPatの+/-キーで表示の変更。
上に並んだ数時キーおよびその下の Q～Rを押すと
白魔法や黒魔法のトップへジャンプします。
スペースキーを押すと再度 再生します。
```

実測（`WM_KEYDOWN/UP` を PostMessage）:

- **NumPad `-` を 47 回** → タイトルが `FFXITool5 シェル [2848] ROM\10\56.DAT` から
  **`FFXITool5 ケアル [2801] ROM\10\9.DAT`** に変化（Cure に切替できた）
- **Space** → 再生し直し

### 1.2 3D ビューの実サイズ（固定 8 px）

| 条件 | クライアント | 3D ビューの幅 | 備考 |
|---|---|---|---|
| 既定ウィンドウ | 624x441 | **8 px**（x=0..7 が全高） | 残りはログ（背景 `(42,23,45)`） |
| 最大化 | 2560x1377 | **8 px（変わらない）** | ウィンドウサイズに比例しない＝固定 |

- `PrintWindow(PW_RENDERFULLCONTENT)` でもログしか取れない（D3D8 の描画面は含まれない）。
- したがって **ffxitool5 単体での 3D 画素比較は不可能**。このツールの実質的な出力は
  **診断ログ**（`type:XX Unknown. Doesnt work` / `found ID` / `not found ID`）である。
- 見た目の比較は **AltanaView の参照画像**（`docs/shots/2026-11-13-cure-av-*.png`）で行う。


## 2. ffxitool5 の色式（テクスチャステージ実測）

`FUN_00405610`（D3D8 `SetTextureStageState` = vtable `+0xFC`）:

```
stage0: COLOROP=6 (MODULATE4X)  COLORARG1=2 (TEXTURE)  COLORARG2=0 (DIFFUSE)
        ALPHAOP=6 (MODULATE4X)  ALPHAARG1=2 (TEXTURE)  ALPHAARG2=0 (DIFFUSE)
        MAGFILTER=2 / MINFILTER=2 (LINEAR)
render: ALPHABLENDENABLE=1 / SRCBLEND=5 (SRCALPHA) / DESTBLEND=2 (ONE) / BLENDOP=1 (ADD)
        ZENABLE=0 / ZWRITEENABLE=0 / CULLMODE=1 (NONE)
```

式（D3D8 固定機能）:

```
color = saturate(tex.rgb * vertex.rgb * 4)
alpha = saturate(tex.a   * vertex.a   * 4)
out   = src * alpha + dst * 1        （加算）
```

## 3. 当方の実装

- `Gfx`：`gGain`（x=RGB 倍率 / y=α 倍率）を定数バッファへ。PS は
  `saturate(tex * col * gGain)`。既定 **1.5 / 1.0**（従来と同一）。
- CLI：
  - `--colorop ffxi|simple`（ffxi = RGB×4 / α×4、simple = 1.5 / 1）
  - `--gain N`（RGB）、`--alphagain N`（α）
  - `--notex on|off`（白テクスチャで色を切り分け）
- 既定は従来どおり（`tools/Verify-All.ps1` は ALL PASS）。

---

## 4. 実測（効果画素の平均色 / Cure）

| 画像・条件 | 平均 RGB | B/R |
|---|---|---|
| **参照（AltanaView Cure 1.5 s）** | **(96.3, 76.6, 101.8)** | **1.06** |
| 当方 既定（tex×col×1.5 / α×1） | (24.2, 24.1, 70.9) | 2.93 |
| 当方 `--colorop ffxi`（×4 / ×4） | (80.1, 76.9, 156.1) | 1.95 |
| 当方 `--notex on`（白・既定ゲイン） | (87.9, 87.1, 192.3) | 2.19 |
| 当方 `--notex on --colorop ffxi` | (223.7, 223.7, 255.0) | 1.14（飽和） |

解釈:

- **明るさ**は ×4（α も含む）で参照の水準に到達する（24→80、参照 96）。
  従来は α が ×1 だったため、加算の寄与が 1/4 以下になっていた。
- **白テクスチャで R=G が一致**（87.9 / 87.1）＝当方の頂点色は **(0.23, 0.23, 0.50)**。
  赤と緑が同値というのは Cure の色としては不自然で、**色スロット／チャネル順の疑い**が強い。
- B/R はテクスチャ込みで 2.93、白で 2.19。参照は 1.06 なので、
  **テクスチャ段（MODULATE4X）を合わせても青偏重は残る**＝頂点色側の問題。

---

## 5. 残差の切り分け（次の一手）

| # | 調査項目 | 根拠 |
|---|---|---|
| 1 | VM#1 `0x16` の 4 バイトの**並び**（RGBA / BGRA） | `--color bgr` で平均が変わらない（テクスチャの R≈B のため判別不能）。バイト列の実測が必要 |
| 2 | VM#2 `0x18..0x1B`（Color スロット）の dest 対応 `(op+0x11)*4` | R=G になる原因（0x18/0x19 の取り違え）を疑う |
| 3 | `0x2D`（キー名 `k1a0`）の実装 | 未実装。α トラックの可能性が高い（kr10/kr11/gr00 が持つ） |
| 4 | 参照は AltanaView（D3D9・別ゲイン） | 完全一致ではなく**傾向一致**を目標にする |

---

## 6. 既定への影響

- 既定（`--colorop simple` = RGB×1.5 / α×1）は**従来と同一**。
  `tools/Verify-All.ps1`（ゴールデン 4 本 / tgt0 / selftest / 決定論 / 異常系）は **ALL PASS**。
- `--ffxi on` は **α も ×4** になった（以前は RGB のみ）。解析用モードの差分として扱う。
