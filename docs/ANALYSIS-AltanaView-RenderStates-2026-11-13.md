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
| 6 | View → Texture / Bilinear / Smooth Shade / Light の実装 | 未計測（Alpha/Wireframe のみ） | 同じ手順で `WM_COMMAND` を送り、`SetTexture(NULL)` / `SetSamplerState` / `SHADEMODE` / `LIGHTING` の差分を見る |
| 7 | 粒子の頂点そのもの（寸法・向き） | `Lock` の `ppbData` は取得済みだが Draw との対応が未確定 | `Lock` 中のポインタ + `DrawPrimitive` の start/prim を同時に記録 |

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

## 11. View メニューの実装（実測・2026-11-13 追加）

メニュー構成（`GetMenu` / `GetMenuStringW` で実測。`&` はアクセラレータ）:

| メニュー | コマンド ID | 内容 |
|---|---|---|
| File → Pause | **40002** | 時計を止める（**描画は継続**。CPU 約 34% で回り続ける） |
| File → Save Image | 40028 (F11) | |
| File → Copy Image | 40029 | |
| File → Exit | 57665 | |
| **View → Alpha** | **40013** | 後述（D3D ステートを変えない） |
| View → Texture | 40014 | |
| View → Bilinear Filter | 40015 | |
| View → Smooth Shade | 40016 | |
| View → Light | 40017 | |
| **View → Wireframe** | **40018** | **`D3DRS_FILLMODE = 2`（WIREFRAME）** |
| View → Camera Rotation | (サブメニュー) | |
| View → Auto Adjust Camera | 40019 | |
| View → Reset Camera | 40020 | |

### 11.1 Wireframe（40018）

- `WM_COMMAND(40018)` → 次フレームで `SetRenderState(D3DRS_FILLMODE, 2)` が 1 回だけ発行される（実測: frame 498）
- 画像: **キャラクタの三角形が線画になる**（`%TEMP%\av_spike\crop_q_wire.png`）
- **粒子（Cure の星）はワイヤーフレームでも見えない** → 粒子パスでは `FILLMODE` が
  モデルパスと別（おそらくソフト側で面を組んでいる、または粒子が細線で視認できない）

### 11.2 Alpha（40013）— ここが最重要

- `WM_COMMAND(40013)` では **D3D のレンダーステート/テクスチャステージが一切変化しない**
  （`SetRenderState` を全 ID 監視しても差分ゼロ）
- しかし**画像は大きく変わる**（Pause で固定した同一フレームで比較）:

| 比較（Pause 中・同一フレーム） | 変化点 | 差分量 |
|---|---|---|
| Alpha **ON** vs **OFF** | 4,434 点 | 317,564 |
| Alpha OFF vs Wireframe | 3,278 点 | 968,576 |

- 見た目（`crop_q_on.png` / `crop_q_alphaoff.png`）:
  - **ON**: 白い四芒星の粒子（Cure）＋ 頭部の紫の発光
  - **OFF**: **粒子が完全に消える**（キャラクタは残る）
- 解釈: `ALPHATESTENABLE=1`（ref=32, GREATER）は**常に ON のまま**で、
  Alpha メニューは **CPU 側で粒子の α を 0 にする**（＝αテストで全滅）と考えるのが自然。
  - 根拠: αテストと加算合成が有効なまま、αだけが変われば「消える」挙動と一致
  - 対立仮説（αブレンドの ON/OFF）は `ALPHABLENDENABLE` が変化しないため否定

> **EffectTest への含意**: α テストを入れないと、Alpha OFF 相当（＝α 0 の粒子）が
> 加算で「見えない」のではなく「黒い板」として残る。Cure の見た目差の主因。

### 11.3 1 フレームの構成（同一フレーム内の順序）

`SetTexture` / `SetTextureStageState` / `SetTransform` / `DrawPrimitive` を時系列で並べると:

```
Clear
COLOROP=2 (SELECTARG1)  + ADDRESSU=CLAMP
View/Proj
SetTexture(0, texA) → 8 枚を順に切替（モデルの材質）
COLOROP=5 (MODULATE2X)          ← ★エフェクト用パスに切替
View/Proj
（粒子の DrawPrimitive 群）
```

→ **COLOROP は 1 フレーム内で 2 に戻したり 5 にしたり**している。
`GetTextureStageState` で見えた 5/6 は「エフェクトパス実行中」の値。

---

## 12. Cure 系の解析（`10/9` → `10/12` の訂正を含む）

`List\Effect\WhiteMagic.csv` の 1 行目 `10/9,Cure` から特定（`List\Effect\index.csv` の
先頭が WhiteMagic = `[Effect] List=0`、`Item=0` が Cure）。

### 12.1 データ（`--selftest`、2026-11-10 ビルド）

```
chunks=29 generators=8 duration=3.3333s
  prn1 frames=9  0.1667s / kr30 frames=4  / kr10 frames=6 / gr00 frames=185 3.1s
  rn00 frames=9 / prn0 frames=9 / kr11 frames=1 / kr12 frames=7
timeline: 0x01(V) 0x56(30) 0x3C(shwh) 0x53(0303) 0x0B(0303) 0x31 0x03(tgt0) 0x32(193) 0x00
```

- 全 generator が **type 0x05**（テクスチャフレーム）
- `kr*` 系は `pos.y` が毎フレーム減少（0 → -2.37）、`scl=(0.024, 0.000, 1.000)`
- 全 generator の **`col` の α が 0.00**（`colorfallback on` で 1 に置換されて描かれる）
- リンク名: `gr`（64×64）、`kir1`（64×64）、`ring`（32×32）

### 12.2 AltanaView の見た目

- 白い**四芒星**の粒子がキャラクタの周囲に散在（`docs/shots/2026-11-13-cure-alpha-on.png`）
- 頭部に紫の発光（別 generator）
- Alpha を切ると**粒子だけ消える**（`docs/shots/2026-11-13-cure-alpha-off.png`）
- ワイヤーフレームにしても**星は線画にならない**（`docs/shots/2026-11-13-cure-wireframe.png`）

### 12.3 現行 EffectTest の出力（既定 `--frames 90`）

| 指標 | 値 |
|---|---|
| `nonBg` / `maxSum` | 3,786 / 309 |
| bbox / aspect / fill / hollow / peak | 137×493 / **0.278** / 0.898 / **6.829** / 0.029 |

→ **縦長の暗い筋**（星ではない）。`hollow=6.83` は「中央に集中」を意味する。

### 12.4 差分の切り分け（本案の結論）

| 要因 | AltanaView | EffectTest | 影響 |
|---|---|---|---|
| αテスト | 有効（ref 32 / GREATER） | **無効** | 星の「硬い縁」が出ない／α 0 の粒が黒板として残る |
| 合成 | `SRCALPHA`+`ONE`（加算） | 既定 `alpha` | 暗く・にじむ |
| 色 | stage0 `MODULATE2X` / `MODULATE4X` | `×1.5` / 等倍 | 芯が白く飛ばない |
| α の由来 | サブ α（4 倍してテスト） | `colorfallback` で 1 固定 | α 0 のデータが「見える」→ 形が崩れる |
| カメラ | dist 2.785 / fovY 57.3° / near 0.125 | dist 14 / 45° / 0.1 | 粒子が小さく見える |

> **注**: `--colorfallback on` は Cure のように **α=0 が正常値**のデータでは
> 「見えてはいけない粒を見せている」可能性がある。α テストと併用したときの
> 正味の見え方を A/B する必要がある（設計時の検証項目）。

### 12.6 訂正（2026-11-13 追記）— 比較対象の DAT が違っていた

- AltanaView のスクリーンショットのステータスバーは **`1/10/12.DAT`**（＝**Cure IV**）を表示していた。
  一方、上の §12.1〜12.5 は `10/9.DAT`（**Cure**）を解析していた（`WhiteMagic.csv` の 1 行目）。
- つまり **別のエフェクト同士を比較していた**。同じ DAT で取り直した結果:

| 対象 | 出力 |
|---|---|
| AltanaView `10/12`（Cure IV） | 白い四芒星の粒子が周囲に散在 ＋ 頭部の紫発光 |
| EffectTest `10/12`（既定） | **暗い円錐（ロケット状）**。`nonBg` 2,647 / `maxSum` 401、bbox 135×367、aspect 0.368、hollow 2.730（`docs/shots/2026-11-13-cure4-effecttest.png`） |

- `10/12` の `--trace`（190 フレーム、917 サンプル）:
  **15 generator のうち 13 個が `x=0, z=0`**（真下に一直線）、**α=0.00**。
  `kir0`/`kir1` だけが `z=-35/-73` へ流れ、`α=0.50`。
- `10/12` の VM#1 実行 op: `0x72`(917) `0x2D`(829) `0x0B`(729) `0x27/0x28`(630) `0x2F`(373)
  `0x02`(328) `0x29`(240) `0x12`(222) `0x2E`(133) `0x03/0x08/0x1F/0x3D/0x3E`(88) `0x07`(35)
  → **`0x72` / `0x2D` / `0x3D` / `0x3E` / `0x07` は未実装**。
- VM#2 は `0x1D`(29,204) `0x08`(20,756) が多く、**`0x1D` は未実装**。

> **結論（現時点）**: 「花火のような破片」に見えない理由は、**位置が散らない（x=z=0）＋α=0** の 2 点。
> 描画ステートの修正だけでは足りず、**VM 側の位置/α の生成**を確定させる必要がある。
> 次の一手は「AltanaView の生の粒子構造体（`obj+0xA0`, 0x108 B）を読んで、
> 位置・色（`+0xA4..0xB0`）・行列（`+0x40`）を我々の VM 出力と**フィールド単位で比較**する」。


### 12.7 再現コマンド（要リビルド。下記 §13 参照）

```powershell
$ROM = 'C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
.\x64\Release\EffectTest.exe --shot "$ROM\10\12.DAT" cure4.png --frames 190
tools\ShotStats.ps1 -Path cure4.png ; tools\RingStats.ps1 -Path cure4.png
```

---

### 12.8 AltanaView が実際に送っている頂点（実測・決定版）

`IDirect3DVertexBuffer9::Lock` の `ppbData` を **Unlock 時点でコピー**し、
`DrawPrimitive` の `(StartVertex, PrimitiveCount)` でデコードした（Cure IV `10/12`、3 秒間）。

| 項目 | 実測 |
|---|---|
| FVF / stride | `0x152` / **36 B** |
| 頂点レイアウト（推定確定） | `+0` float3 pos, `+12` float（0.0 / 4.01）, `+24` **D3DCOLOR**, `+28/+32` uv |
| 位置範囲（183 頂点） | **x ∈ [-1.15, 0.12] / y ∈ [-2.01, 0.00] / z ∈ [-0.81, 0.93]** |
| クワッドの辺 | 約 **0.45 m**（例: (-0.816,0,-0.808) → (-1.064,0,-0.434) = 0.448） |
| 頂点色（α） | **0x01〜0x40（1〜64 / 255）** = 非常に薄い（加算＋α4 倍で見える） |
| ドロー数 | 23 ドロー / 183 頂点（3 秒）＝ **1 ドローに複数クワッドをバッチ** |

**これは我々との決定的な差**:

| | AltanaView（実測） | EffectTest（実測） |
|---|---|---|
| 位置 | x/z が ±1 m に散在 | **x=0, z=0 の一直線** |
| α | 頂点色 α=1〜64 | **α=0.00**（フォールバックで 1 に置換） |
| 枚数 | 1 ドローに複数 | インスタンス毎に 1 クワッド |

> つまり **描画ステート以前に、VM が生成する位置とαが違う**。
> 次の一手は「AltanaView の粒子構造体（`obj+0xA0`, 0x108 B, 色=`+0xA4..0xB0`）を
> 毎フレーム読んで、我々の `--trace` と generator 単位で対応付ける」こと。

---

## 13. 付記（重要・次回の前提）

1. **`x64\Release\EffectTest.exe` は 2026-11-10 ビルド**で、2026-11-12 追加の CLI
   （`--colorfallback` / `--arrange` / `--ringplane` / `--ringscale` / `--spiral turns` 等）が
   **入っていない**。未知のフラグは位置引数として食われ、`png=off` のような
   不可解な挙動になる（実測）。**A/B の前に必ずリビルド**する。
2. **デバイス vtable は +1 ずれている**（この AltanaView のサンク表）:
   `80`→`81 DrawPrimitive`, `81`→`82 DrawIndexedPrimitive`, `99`→`100 SetStreamSource`,
   `101`→`102 SetStreamSourceFreq`, `103`→`104 SetIndices`, `88`→`89 SetFVF`。
   `42 EndScene` / `44 SetTransform` / `47 SetViewport` / `57 SetRenderState` /
   `65 SetTexture` / `67 SetTextureStageState` / `69 SetSamplerState` は**ずれなし**。
   → スロットは必ず `ret imm16`（引数バイト数）で確認する。
3. **頂点バッファ**: `SetStreamSource(0, vb, 0, stride=32)` を毎バッチで設定し、
   `Lock(0, size, &p, D3DLOCK_DISCARD)` → 書き込み → `Unlock` を**描画ごとに繰り返す**
   （計測中に 8,578 回）。頂点の中身の取得は未完了（次回 `Lock` の `ppbData` から読む）。
4. 計測に使ったスクリプトは `%TEMP%\av_spike\`（`probe7.js` 状態ログ / `probe8..11.js` 頂点・スロット調査 /
   `drive5.ps1` メニュー操作）。`AltanaView.ini` は `AltanaView.ini.bak` から復元済み。

---

## 14. 参照



| 文書 | 内容 |
|---|---|
| `docs/ANALYSIS-AltanaView-Draw.md` §2 | `0x21` の頂点 24 B・FVF `0x142`（本計測と一致） |
| `docs/ReadMe.md` §忠実度 | 本計測で「仮定」を置換（未更新） |
| `docs/GOLDEN.md` | 本計測の反映は未実施（コード変更前のため） |



