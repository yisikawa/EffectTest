# AltanaView の描画ステート実測（D3D9 動的計測・2026-11-13）

対象: `D:\tool\AltanaView-master\AltanaView.exe`（UPX パック / 182,272 B / sha256 `ba7b9e4c…b674`）
手段: **中断起動 → Frida で D3D9 をフック → 再開**（AltanaView の実 `IDirect3DDevice9` 呼び出しを採取）
種別: 計測スパイク（コード変更なし。製品コードは未変更）

> 目的: これまで「仮定」だった `docs/ReadMe.md` §忠実度「描画の厳密な設定」を実測で置き換える。
> 本ファイルの数値は **1 回の計測（同一プロセス）** に基づく。再現手順は §10。

---

## 0. 結論（要約）

| 項目 | 実測値（AltanaView） | 現行 EffectTest |
|---|---|---|
| 合成 | `ALPHABLENDENABLE=1` / `SRCBLEND=SRCALPHA(5)` / `DESTBLEND=ONE(2)` / `BLENDOP=ADD(1)` = **加算** | 既定 `alpha`（`SRC_ALPHA`/`INV_SRC_ALPHA`） |
| Z | `ZENABLE=1` / **`ZWRITEENABLE=0`** / `ZFUNC` は既定（LESSEQUAL） | `DepthEnable=TRUE` / `DepthWriteMask=ZERO` ✓ |
| αテスト | **`ALPHATESTENABLE=1` / `ALPHAREF=32` / `ALPHAFUNC=GREATER(5)`** | **なし** |
| カリング | `CULLMODE=NONE(1)` | 既定（D3D11 CULL_BACK 相当？未設定のまま） |
| ライティング | `LIGHTING=0` / `AMBIENT=0xFFFFFFFF` | 該当なし |
| テクスチャ段 | stage0: **`COLOROP=MODULATE2X(5)` / `ALPHAOP=MODULATE4X(6)`**（両方 arg1=TEXTURE, arg2=CURRENT）/ stage1〜7 は DISABLE | PS で `rgb*col.rgb*1.5`, `a=t.a*col.a` |
| サンプラ | MIN/MAG=**LINEAR** / MIP=**NONE** / ADDRESS=**CLAMP**(U,V) | MIN_MAG_MIP_LINEAR + CLAMP ✓（MIP のみ差） |
| 頂点 | エフェクトは **FVF `0x142`**（XYZ\|DIFFUSE\|TEX1 2D = 24 B）／`0x152` も併用 | pos3+col+uv（24 B）✓ |
| 描画 | `DrawPrimitive`（3 引数）を **バッチ**（prim=1〜128+）で発行。`WORLD` は**単位行列**（頂点はワールド座標） | インスタンス毎に 6 頂点 |
| カメラ | 距離 **2.785**（eye=(0.008, 0.985, 2.605)）/ **fovY≈57.3°** / near **0.125** / far **64** | dist 14 / fovY 45° / 0.1 / 1000 |
| 背景 | 実測 RGB(128,255,255)（設定 `BackColor=4286644223`） | `--bg` で指定（参照は 40F0F0） |

**最重要**: 明るさの正体は「×1.5 のゲイン」ではなく
**stage0 の `MODULATE2X`（RGB 2 倍）と `MODULATE4X`（α 4 倍）**。
さらに **α テスト（ref=32/255, GREATER）** が効いている。

---

## 1. 計測方法（なぜ「中断起動」か）

AltanaView は起動直後に `LoadLibraryA("d3d9.dll")` → `GetProcAddress` → `Direct3DCreate9` を呼ぶため、
起動後に attach するとデバイス生成を取り逃がす。そこで:

1. `CreateProcess(CREATE_SUSPENDED)` で起動（CWD = `D:\tool\AltanaView-master`）
2. その PID に Frida attach
3. `LoadLibrary*` / `GetProcAddress` をフック → `Direct3DCreate9`（および Ex）を捕捉
4. メインスレッドを再開（`ResumeThread`）
5. `IDirect3D9::CreateDevice` → 返ったデバイスの vptr を取得し、
   `EndScene`/`SetRenderState`/`SetTexture`/`DrawPrimitive` 等をフック

スロット対応は推測せず、**各エントリの `ret imm16`（引数バイト数）**で確定した（§5・§7）。

> 実測: `d3d9.dll` base `0x63460000` / デバイス `0x3eaf360` / vptr `0x3eb22bc`
> （vptr は d3d9.dll 内の関数を指すヒープ上のサンク表。**D3D メソッドをフック内から呼ぶと再入でクラッシュ**する）

証跡（REA Evidence）: `ev_74c4bdae…`（フック導入）, `ev_e1fb7963…`（per-draw 計測）

---

## 2. デバイス生成（`IDirect3D9::CreateDevice`）

| 項目 | 値 | 意味 |
|---|---|---|
| BackBufferWidth/Height | **819 × 672** | 3D ビュー（ウィンドウ 1180×820 時） |
| BackBufferFormat | 0 (`D3DFMT_UNKNOWN`) | ウィンドウ時はデスクトップ形式に追従 |
| BackBufferCount | 0（既定 1） | |
| MultiSampleType | 0 (NONE) | |
| SwapEffect | 1 (`DISCARD`) | |
| Windowed | 1 | |
| **EnableAutoDepthStencil** | **1** | |
| **AutoDepthStencilFormat** | **80 = `D3DFMT_D16`** | **16 bit 深度** |
| PresentationInterval | 0 (`DEFAULT`) | vsync |
| Flags | 2 (`DISCARD_DEPTHSTENCIL`) | |
| DeviceType / Behavior | 1 (HAL) / **0x40** (`HARDWARE_VERTEXPROCESSING`) | |

> **含意**: 深度は **D16**。near 0.125 / far 64 と組み合わせると 16 bit の分解能は
> 実効 1/65536 程度で、遠方の粒子は Z ファイトしやすい。EffectTest は D24 既定なので
> 比較時に差が出る可能性がある（要確認）。

---

## 3. レンダーステート（エフェクト再生中）

起動直後〜再生中の全 `SetRenderState` を採取した結果（変化したもののみ）:

| D3DRS | 値 | 意味 |
|---|---|---|
| `ALPHATESTENABLE` (15) | 1 | α テスト有効 |
| `ALPHAREF` (24) | **32** | しきい値 32/255 = 0.1255 |
| `ALPHAFUNC` (25) | **5** | `D3DCMP_GREATER`（ref より大きいとき描く） |
| `ALPHABLENDENABLE` (27) | 1 | |
| `SRCBLEND` (19) | 5 | `D3DBLEND_SRCALPHA` |
| `DESTBLEND` (20) | 2 | `D3DBLEND_ONE` |
| `BLENDOP` (171) | 1 | `D3DBLENDOP_ADD` |
| `ZENABLE` (7) | 1 | |
| **`ZWRITEENABLE` (14)** | **0** | 深度は書かない |
| `CULLMODE` (22) | 1 | `D3DCULL_NONE`（両面） |
| `LIGHTING` (137) | 0 | 固定パイプライン無効 |
| `AMBIENT` (139) | 0xFFFFFFFF | 白 |
| `ZFUNC` / `FOGENABLE` / `SPECULARENABLE` / `SEPARATEALPHABLENDENABLE` | 設定なし | 既定値（LESSEQUAL / FALSE / TRUE / FALSE） |

**ブレンド式**: `dst = dst + src × srcA`（**加算**、可換なので描画順に依存しない）

> 注: `ZWRITEENABLE=0` は **エフェクト再生開始後**に設定される（起動直後の最初の 1〜2 フレームは既定の 1）。
> `docs/ReadMe.md` が「実設定は未確認」としていた `0x40b8b0` の静的観測
> （`SRCALPHA`/`ONE`/`ZWRITE=0`）と**一致**した。

**モデル（PC タブ）の描画では別の組み合わせ**（`BLENDOP=3` / `DESTBLEND=6` が交互）を観測した。
エフェクト経路とは別なので §9 に未確定として残す。

---

## 4. テクスチャステージ / サンプラ

`GetTextureStageState`（stage 0〜7）と `GetSamplerState`（0〜3）で読み出した実値:

| stage | COLOROP | COLORARG1/2 | ALPHAOP | ALPHAARG1/2 | TEXCOORDINDEX |
|---|---|---|---|---|---|
| 0 | **5 = MODULATE2X** | 2 (TEXTURE) / 1 (CURRENT) | **6 = MODULATE4X** | 2 (TEXTURE) / 1 (CURRENT) | 0 |
| 1〜7 | 1 = DISABLE | — | 1 = DISABLE | — | n |

| サンプラ | 値 |
|---|---|
| ADDRESSU / ADDRESSV | 3 = `CLAMP` / 3 = `CLAMP` |
| ADDRESSW | 1 = `WRAP` |
| MAGFILTER / MINFILTER | 2 = `LINEAR` / 2 = `LINEAR` |
| MIPFILTER | **0 = `NONE`** |
| MAXMIPLEVEL | 1 |

**計算式（stage0, FFP）**

```
color = clamp2x( tex.rgb × vertexDiffuse.rgb )   // MODULATE2X
alpha = clamp4x( tex.a  × vertexDiffuse.a   )    // MODULATE4X
// その後 ALPHATEST(ref=32, GREATER) → SRCALPHA/ONE 加算合成
```

- `clamp2x` / `clamp4x` は 0..1 にクランプ（D3D9 のステージ出力は 0..1）
- つまり **α は 4 倍してから α テストされる**ため、`tex.a × col.a > 0.0314` で残る
- `COLORARG2=CURRENT` は stage0 では **頂点ディフューズ**（FVF に `DIFFUSE` があるため）

---

## 5. 頂点フォーマット（FVF 実測）

`SetFVF`（vtable スロット 89、引数 1 個）の値を per-draw で採取:

| FVF | 構成 | サイズ | 出現 |
|---|---|---|---|
| `0x142` | XYZ \| DIFFUSE \| TEX1(2D) | 24 B | エフェクトのビルボード（静的解析 §2.1 と一致） |
| `0x152` | XYZ \| DIFFUSE \| TEX1(2D, size 明示) | 24 B | 同上（別オブジェクト） |
| `0x42` | XYZ \| DIFFUSE | 16 B | テクスチャ無しのポリゴン |
| `0x112` | XYZ \| TEX1(2D) | 20 B | **キャラクタ（モデル）**（頂点色なし） |

- 60 フレーム時点の内訳: `0x112`×60 / `0x152`×73 / `0x42`×14 / `0x142`×34
- `WORLD` 行列は **単位行列**（`GetTransform(D3DTS_WORLD)`）
  → **頂点はワールド座標で書き込まれている**（= ビルボードは CPU 側で生成）
- したがって EffectTest の「シェーダ内で常にカメラ正対」は**結果としては等価**
  （加算合成は順序非依存なので、並び替えの差も出ない）

---

## 6. カメラと投影（`GetTransform` 実測）

VIEW（`D3DTS_VIEW`）:

```
[-0.2851  -0.1934   0.9388  0 ]
[ 0.0000  -0.9794  -0.2017  0 ]
[ 0.9585  -0.0575   0.2792  0 ]
[-0.0082  -0.9848  -2.6052  1 ]
```

→ 平行移動が `(-0.008, -0.985, -2.605)`。`AltanaView.ini` の
`Translation=(0.008, 0.985, 2.605)` の**逆行列**に一致（距離 **2.785**）。

PROJECTION（`D3DTS_PROJECTION`）:

```
[1.5019  0       0       0]
[0       1.8305  0       0]
[0       0      -1.00196 -1]
[0       0      -0.12524 0]
```

| 導出 | 値 |
|---|---|
| `m11 = cot(fovY/2)` | 1.8305 → **fovY = 57.32°** |
| `m00 = cot(fovX/2)` | 1.5019（aspect 1.2188 = 819/672） |
| `m22 = zf/(zf-zn)` | 1.001956 → **zn = 0.125**, **zf = 64.0** |

- 投影は `D3DXMatrixPerspectiveFovLH` 相当（`m32=-1`）
- **EffectTest との差**: fovY 45°→57.3°、near 0.1→0.125、far 1000→64、dist 14→2.785
- ピッチは `asin(0.985/2.785) = 20.7° = 0.361 rad`（現行既定 0.35 とほぼ同じ）
- ヨーは `π - 0.003`（現行既定 π とほぼ同じ）

`SetViewport` は**呼ばれていない** → 既定（バックバッファ全面 = 819×672）。

---

## 7. 描画呼び出し

| 項目 | 実測 |
|---|---|
| 使用 API | **`DrawPrimitive` のみ**（`DrawIndexedPrimitive`/`UP` 系は 0 回） |
| 引数 | `(D3DPRIMITIVETYPE=4 /*TRIANGLELIST*/, StartVertex, PrimitiveCount)` |
| 発行数 | 約 **2,500 回/秒**（60fps で約 42/フレーム、再生中は最大 1,400 超/フレーム） |
| 単位 | **バッチ**（`PrimitiveCount` = 1, 8, 17, 24, 64, 103, 128 …）。粒子 1 個 = 6 頂点ではない |
| テクスチャ | `SetTexture(0, …)` で切替（採取中に 8 種類以上） |
| シェーダ | `SetVertexShader(NULL)` / `SetPixelShader(NULL)` = **固定パイプライン** |
| マテリアル | `SetMaterial` は**呼ばれない** → 色は**頂点ディフューズ**（FVF の DIFFUSE） |

> 参考: vtable スロットの実測対応（`ret imm16` と引数形から確定）
> `42 EndScene(0)` / `44 SetTransform(2)` / `47 SetViewport(1)` / `57 SetRenderState(2)` /
> `65 SetTexture(2)` / `67 SetTextureStageState(3)` / `69 SetSamplerState(3)` /
> **`81 DrawPrimitive(3)`** / `82 DrawIndexedPrimitive(6)` / **`89 SetFVF(1)`**

---

## 8. 現行 EffectTest との差分（実装候補）

| # | 差分 | 現行 | AltanaView 実測 | 修正案（最小） |
|---|---|---|---|---|
| 1 | 合成 | `--blend` 既定 `alpha` | `SRCALPHA`/`ONE`（加算） | 既定を `alpha_add` に。`--blend alpha` で旧比較を維持 |
| 2 | αテスト | なし | `ref=32/255`, `GREATER` | PS に `clip(alpha - 32.0/255.0)` 相当（`<=` を捨てる） |
| 3 | 明るさ | `rgb*col.rgb*1.5`, `a=t.a*col.a` | RGB **×2**, α **×4**（0..1 クランプ後 α テスト） | PS を `saturate(t.rgb*2)*col.rgb` / `saturate(t.a*4)*col.a` に。`--gain` は相殺用に残す |
| 4 | カメラ | dist 14 / fovY 45° / 0.1-1000 | dist **2.785** / fovY **57.32°** / **0.125-64** | `--dist 2.785` を既定に、fov/near/far を定数化（`--fov/--near/--far`） |
| 5 | サンプラ | `MIN_MAG_MIP_LINEAR` | MIN/MAG LINEAR, **MIP NONE** | `MIN_MAG_LINEAR_MIP_POINT` 相当に |
| 6 | 深度 | D24 既定 | **D16** | 比較用に `--depth16` を追加（既定は現状維持） |

> **注意**: #1〜#3 は `docs/GOLDEN.md` の `--shot` 値を**全面的に変える**。
> 実施するなら「新ゴールデン」として再基準化し、旧値はフラグで再現できるようにする
> （既存の `--array line` 等と同じ扱い）。

**期待される効果**
- 加算合成 + α テスト + ×2/×4 で、参照 `frames/v_0062.png` の「芯が白く飛び、外周が薄い」見た目に近づく
- 現行の「×1.5 + 通常α」は **暗く・にじむ**方向にずれている

---

## 9. 未確定（次にやること）

| # | 項目 | 現状 | 調べ方 |
|---|---|---|---|
| 1 | モデル（PC タブ）経路の `BLENDOP=3` / `DESTBLEND=6` | 交互に設定される。`REVSUBTRACT`+`INVSRCALPHA` は異常に見える | モデル描画中の `SetRenderState` を時系列で採取（同一手順） |
| 2 | 頂点バッファの中身 | ワールド座標の 24 B 頂点であることは FVF と WORLD=単位行列から確定。サイズ・向きは未採取 | `IDirect3DVertexBuffer9::Lock/Unlock` をフックして先頭頂点を読む |
| 3 | `Clear` の色 | 画面からは RGB(128,255,255) を採取。`Clear` フックは引数型で失敗 | 引数を生バイトで読む／`D3DRS_*` ではなく `Clear` の Color を直接記録 |
| 4 | 1 フレーム内のバッチ順序 | `DrawPrimitive` の並びは取れている（per-draw ログ 180 件） | テクスチャ毎のまとまり・Z 順の有無を集計 |
| 5 | 深度 D16 の影響 | 未評価 | EffectTest を D16 にして `hollow`/`fill` を比較 |

---

## 10. 再現手順（使い捨てスクリプト／`%TEMP%\av_spike`）

```powershell
# 1) 中断起動（CWD を AltanaView に合わせる）
pwsh -NoProfile -File "$env:TEMP\av_spike\launch_suspended.ps1"   # → pid.txt
# 2) Frida attach（REA: start_frida_session(mode=attach, pid=...)）
# 3) probe4.js をロード（Direct3DCreate9/CreateDevice/デバイスをフック）
# 4) resume.flag を作って再開
New-Item -ItemType File "$env:TEMP\av_spike\resume.flag"
# 5) Effect タブ + Play をクリック（タブ名/矩形を実測してクリック）
pwsh -NoProfile -File "$env:TEMP\av_spike\drive3.ps1"
# 6) 採取結果
Get-Content "$env:TEMP\av_spike\probe6_out.jsonl"
```

| ファイル | 役割 |
|---|---|
| `launch_suspended.ps1` | `CREATE_SUSPENDED` 起動 + `resume.flag` 待ち |
| `probe4.js` | `Direct3DCreate9`→`CreateDevice`→デバイス vtable をフック（初期ステートも捕捉） |
| `probe6.js` | per-draw（FVF・テクスチャ・ステート）採取 |
| `sig.js` | 各スロットの `ret imm16`（引数バイト数）から API を同定 |
| `drive3.ps1` | タブ名/矩形を `TCM_GETITEM(W)/TCM_GETITEMRECT` で実測して Effect タブ→Play |

**ハマりどころ（実測で判明）**
- attach 後に `Direct3DCreate9` をフックしても**間に合わない**（起動直後に生成済み）。中断起動が必須
- **D3D メソッドをフック内から呼ぶと再入でクラッシュ**（`GetRenderState` を `EndScene` 内で呼んで落とした）
- デバイスの vptr は d3d9.dll 内ではなく**ヒープ上のサンク表**。スロット番号は `ret imm16` で確認する
  （本バージョンでは `DrawPrimitive` が **81**、`SetFVF` が **89**）
- タブは `TCS_MULTILINE` で**選択行が入れ替わる**ため、固定座標クリックは外れる（`TCM_GETITEMRECT` を使う）

---

## 11. 参照

| 文書 | 内容 |
|---|---|
| `docs/ANALYSIS-AltanaView-Draw.md` §2 | `0x21` の頂点 24 B・FVF `0x142`（本計測と一致） |
| `docs/ReadMe.md` §忠実度 | 本計測で「仮定」を置換（未更新） |
| `docs/GOLDEN.md` | 本計測の反映は未実施（コード変更前のため） |



