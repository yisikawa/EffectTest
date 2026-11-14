# ffxitool5 の座標系（View / Projection）解析と取り込み — 2026-11-14

対象: `D:\tools\ffxitool13032900\ffxitool5.exe`
（SHA-256 `2b159dc0…f69` / PE32 x86 / D3D8 / 固定ベース 0x400000）

---

## 0. 結論

1. **ffxitool5 は粒子座標の軸変換をしていない**（y/z の入れ替えや符号反転は無い）。
   データの値をそのまま D3D のワールド座標として使う。
2. 「DirectX 座標への変換」に相当するのは **View / Projection の組み方**で、
   ffxitool5 は **右手系（RH）** の LookAt / Perspective を D3D8 に渡している。
3. その結果の実効軸は **右 = −X / 上 = +Y / 奥 = +Z**。
   D3D の LH 既定（右=+X / 奥=−Z）とは **X と奥行きが反転**する
   ＝「反対側（−Z 側）から見た像」になる。
4. 当アプリに `--camera ffxi` として取り込んだ（既定は従来の orbit のまま）。

---

## 1. 根拠（逆コンパイル）

描画は `FUN_00405ab0`。3D のセットアップ部分は次のとおり。

```c
local_1f8[3] = 0.0;  local_1f8[4] = 0.05; local_1f8[5] = -5.0;   // eye
local_1f8[0] = 0.0;  local_1f8[1] = 0.0;  local_1f8[2] = 0.0;    // at
local_1f8[6] = 0.0;  local_1f8[7] = -1.0; local_1f8[8] = 0.0;    // up

FUN_004131bc(view, eye, at, up);          // 0x4131bc: LookAt
SetTransform(device, 2, view);            // D3DTS_VIEW
FUN_00411b22(proj, 0.7853981, 1.3333334, 1.0, 20.0);
SetTransform(device, 3, proj);            // D3DTS_PROJECTION
translation(0,0,0) → SetTransform(device, 0x100, m);   // D3DTS_WORLD
```

- `SetTransform` は vtable `+0x94`（`0x100` = D3DTS_WORLD の実測から確定）。
- **`FUN_004131bc` = RH の LookAt**:
  `z = eye - at` / `x = z × up` / `y = z × x`（正規化し、平移は `-axis·eye`）。
- **`FUN_00411b22` = RH の Perspective**:
  `m00 = cot(fov/2)/aspect`、`m11 = cot(fov/2)`、`m22 = f/(n-f)`、
  `m32 = f·n/(n-f)`、`m23 = -1`（`w = -z`）。
- 値: **fov 45°（0.7853981） / aspect 4:3 固定（1.3333334） / near 1.0 / far 20.0**。
  aspect は**ウィンドウ比を見ていない**（4:3 以外では引き伸ばされる）。

## 2. 実効軸の導出

eye=(0,0.05,−5), at=(0,0,0), up=(0,−1,0) を RH の式へ:

```
Z = normalize(eye - at) = (0, 0.010, -0.99995)
X = normalize(Z × up)   = (-1, 0, 0)          ← 画面の右 = −X
Y = Z × X               = (0, 0.99995, 0.010) ← 画面の上 = +Y
```

- 奥行き `w = -z_cam = Z·(p - eye)` なので **手前は +Z 側**、可視範囲は
  `z ∈ (-4, +15)`（near 1 / far 20 を eye z=−5 から測った値）。
- つまり **カメラは −Z 側から +Z を向き、X は左右反転**して見える。

> 注意: `up=(0,-1,0)` を **LH** の LookAt に渡すと上下も反転してしまう。
> ffxitool5 は RH 式なので、**RH の LookAt/Perspective を組むのが等価**。

---

## 3. 当アプリの現状（取り込み前）

| 項目 | 当アプリ（orbit・既定） | ffxitool5 |
|---|---|---|
| 座標系 | LH（`XMMatrixLookAtLH` / `PerspectiveFovLH`） | **RH**（自前 LookAt / Perspective） |
| eye / at | `+Z` 側（yaw=π, dist 14, pitch 0.35） | `(0, 0.05, -5)` / `(0,0,0)` |
| up | `(0,1,0)` | `(0,-1,0)` を RH に渡す → 実効 `+Y` |
| 実効の画面軸 | 右 +X / 上 +Y / 奥 −Z | **右 −X / 上 +Y / 奥 +Z** |
| 射影 | fov 45°, aspect = W/H, 0.1..1000 | fov 45°, **aspect 4:3 固定**, 1..20 |
| 粒子座標 | DAT の値をそのまま（`--zup on` は実験の軸入れ替え） | **そのまま**（軸変換なし） |

→ 「DX11 の座標系に変換しているか」の答えは **いいえ**（変換していない）。
ただし ffxitool5 も変換しておらず、**カメラで合わせている**ので、
合わせるべきは View/Projection だった。

## 4. 取り込み（2026-11-14 実装）

`--camera ffxi|orbit`（既定 `orbit` = 従来動作）

- `ffxi`:
  - `XMMatrixLookAtRH(eye=(0,0.05,-5), at=(0,0,0), up=(0,1,0))`
  - `XMMatrixPerspectiveFovRH(45°, 4/3, 1, 20)`
  - ビルボードの軸を **右=(-1,0,0) / 上=(0,1,0)** に固定（RH カメラの実効軸）
  - ログ: `[cam] mode=ffxi rh eye=(0,0.05,-5) at=(0,0,0) up=(0,1,0) fov=45 aspect=4:3 zn=1 zf=20 (right=-X up=+Y fwd=+Z)`
- 既定（`orbit`）は従来どおり。ログに `[cam] mode=orbit lh ...` を出す
- 補助: `--axes on|off`（座標軸の表示。既定 off。shot でも使えるようにした）

> 実装は「ffxitool5 の RH 式をそのまま再現」した。
> `up=(0,-1,0)` を LH に渡す置き換えは上下が逆になるため行っていない。

## 5. 検証（実測）

### 5.1 等価性: ffxi と「orbit(yaw0, pitch0.01, dist5, 4:3)」の比較

```powershell
--camera ffxi                                     --res 640x480  # (A)
--camera orbit --yaw 0 --pitch 0.01 --dist 5      --res 640x480  # (B)
```

同じ位置のカメラなので、**水平反転のみ**の関係になるはず。
平均画素差（step=2 / 76,800 サンプル）:

| 比較 | 平均差 |
|---|---|
| A と B をそのまま比較 | **11.778** |
| A と B を左右反転して比較 | **2.378** |

→ **反転側が 5 倍近く一致**（残差は near/far の差 1..20 vs 0.1..1000 と pitch の丸め）。

### 5.2 Cure `tgt0` / 40 フレーム

| カメラ | `nonBg` | `maxSum` |
|---|---|---|
| `ffxi` | 6,433 | 347 |
| `orbit`（同位置） | 6,424 | 346 |

→ 構図が一致（差は near/far による欠けのみ）。

### 5.3 既定の回帰

`--camera` 未指定は従来どおり（`docs/GOLDEN.md` §2 の 4 本は不変）。

## 6. 注意 / 未確定

- 4:3 固定は ffxitool5 の仕様。16:9 で見ると横に伸びる（比較時は `--res 640x480` 等に揃える）。
- `zn=1 / zf=20` も仕様。**eye から 1 未満・20 超は切れる**（`z < -4` は不可視）。
  当アプリの orbit は 0.1..1000 なので、遠方の粒子は ffxi モードでは出ない。
- `--zup on` はデータ軸を入れ替える**実験**で、ffxitool5 は使っていない。
  ffxitool5 に合わせる目的では **off（既定）**のまま `--camera ffxi` を使う。
- ffxitool5 の near/far と fov が**固定値**なのは、エフェクトを原点周辺で見る前提だから。
  実ゲームのカメラ（AltanaView.ini の dist 2.785 等）とは別物。
