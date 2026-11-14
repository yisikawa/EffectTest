# ffxitool5 パーティクル・プール完全解析 — 2026-11-14

> 対象: `D:\tools\ffxitool13032900\ffxitool5.exe`（SHA-256 `2b159dc0…f69`、MSVC・非パック・D3D8）
> 方法: Ghidra 12.1.4 headless（プロジェクト `D:\GhidraProjects\Effect5`、逆コンパイル一括出力 `C:\Users\yisik\AppData\Local\Temp\opencode\ft5\out\`）
> 本書は `ANALYSIS-ffxitool5-2026-11-13.md` の続き。**タイムライン実行・ジェネレータ VM・粒子プールの全容**を確定した。

---

## 0. 結論

1. **タイムライン実行関数 = `FUN_004057a0`**（EffectTest の `EffectContext::Advance()` 相当）。
   命令形式・delay 加算方式・op 0x02/0x03/0x0B の意味が EffectTest 実装と一致した。
2. **ジェネレータ VM = `FUN_004041b0`（パース）+ `FUN_00404750`（放出・積分）+ `FUN_004050b0`（描画）**。
   AltanaView の「2 段 VM」とは**別解釈**だが、generator payload の 4 ストリーム（`+0x80..0x8C`）は共通。
3. **連続放出（パーティクル・プール）の公式仕様を確定**。EffectTest 最優先課題（ReadMe 拡張 #1）の実装根拠が得られた。
4. **色は 0..255 バイトで 0x80=中立**（MODULATE4X ×4 で 1.0 になる）。デフォルト頂点色 = `0x80808080`。
5. 背景クリア色 = `0xFF2A172D`。シーン既定 = SRCALPHA+INVSRCALPHA、粒子ごとに `データ2 type 0x1E` bit2 で ONE/INVSRCALPHA 切替。

---

## 1. フレームループ（`FUN_00406350` = WinMain）

```
33 ms tick (timeGetTime, DVar9 += 0x21):
  if (eff) { FUN_004057a0(eff); FUN_004057a0(eff); }   // Advance ×2 = 60fps 相当
  FUN_00405ab0();                                       // カメラ + シーン描画
```

## 2. エフェクト・オブジェクト（0xC2C B, `FUN_00403ac0` 生成 / `FUN_00405990` 破棄）

| オフセット (dword) | 内容 |
|---|---|
| +0x00 | main チャンク先頭 |
| +0x04 / +0x08 / +0x0C | ストリーム A/B/C = main+0x20 / +0x24 / +0x28（相対） |
| +0x10 | 次命令までの累積フレーム（時間カーソル） |
| +0x14 | 長さ（main+0x2C） |
| +0x18 | DAT バッファポインタ（名前解決に使用） |
| +0x1C | **タイムライン命令カーソル**（dword [2]） |
| +0x1C.. | generator 配列（0x2430 B 各）＋件数 +0x41C |
| +0x420 | サブエフェクト配列（0xC2C B 各）＋件数 +0x820 |
| +0x824 | 描画オブジェクト配列（0x20 B 各）＋件数 +0xC24 |
| +0xC28 | 現在フレーム（ip） |

### `FUN_004057a0` = Advance（タイムライン実行）

```c
if (this[4] <= this[0x30a]) {           // 次命令の時刻が到来
  do {
    op = cur[0]; len = cur[1]; delay = cur[4:6]; param = cur[6:8]; name4 = cur[8:12];
    switch (op) {
      case 0x02: FUN_004055a0(this, param, name4); break;  // ジェネレータ生成
      case 0x03: FUN_00403b90(this, param, name4); break;  // サブエフェクト
      case 0x0B: FUN_00403be0(this, param, name4); break;  // 描画オブジェクト(サウンド)
    }
    cur += len * 4;                     // ★ (len & 0x1F) * 4
    this[4] += delay;                   // ★ delay 加算
  } while (this[4] <= this[0x30a]);
}
this[0x30a]++;                          // ip++
for each generator: FUN_00404750(gen);  // 放出・積分・寿命
for each sub-effect: FUN_004057a0(sub); // 再帰
for each draw-obj:  if (age < lifetime) age++;  // 一度きり発火 (FUN_004058f0 で実行)
```

> **EffectTest の `ExecInst` と同一方式。** `+0xC28`（ドキュメントで「予備カウンタ」としたもの）= ip。

### op ハンドラ

| op | 関数 | 生成物 |
|---|---|---|
| 0x02 | `FUN_004055a0` | generator ctx（0x2430 B）。**op の param を ctx[6]（寿命）に設定**、gen チャンク(type 5) を名前解決 → `FUN_004046d0` → `FUN_004041b0` |
| 0x03 | `FUN_00403b90` | サブエフェクト（0xC2C B）→ `FUN_00403ac0(sub, dat, name4)` |
| 0x0B | `FUN_00403be0` | 描画オブジェクト（0x20 B）。**0x3D チャンク**を名前解決 → `FUN_00403790`（`+0x18` の値を文字列化 → `FUN_00402bc0` でサウンド/リソース生成、一度だけ再生 `FUN_004019f0`） |

## 3. ジェネレータ・コンテキスト（0x2430 B）

| オフセット | 内容 |
|---|---|
| +0x00 | generator チャンク(type 5) |
| +0x04..0x10 | データ1..4 ストリーム = gen + *(gen+0x80/0x84/0x88/0x8C) |
| +0x14 | （0） |
| +0x18 | 粒子配列の先頭（+0x24..0x1024、各 0x5C B） |
| +0x1C | **寿命（データ2 type1 op の +0x22 word）** ← 粒子の最終年齢 |
| +0x24..0x1024 | 粒子配列（dword [9..]） |
| +0x1024 | 現在の粒子数 [0x409] |
| +0x1028 | 放出済み総数 [0x40A]（円環の位相に使用） |
| +0x102C | 形状オブジェクト（0x1F/0x21/0x25 由来） |
| +0x1030..0x142C | データ1 op テーブル（type*4） — **全 type 未実装（ログのみ）** |
| +0x1430..0x182C | データ2 op テーブル（type*4 = 命令レコード） |
| +0x1830..0x1C2C | データ3 op テーブル — **積分の有効化フラグとして消費** |
| +0x1C30..0x202C | データ4 op テーブル — **全 type 未実装** |
| +0x2030..0x242C | データ2 キートラック値表（type*4 = 0x19 チャンク+0x10） |

### 粒子構造体（0x5C B）

| dword | 内容 | 由来（データ2 type） |
|---|---|---|
| [0] | 年齢 | — |
| [1..3] | 位置 xyz | type 1（初期値）/ type 2（速度）|
| [4..6] | 回転 xyz 初期値 | type 9 / type 0xA（乱数） |
| [7..9] | スケール xyz（初期 1.0） | type 0xF / 0x10（乱数）/ 0x11（一様乱数） |
| [0xC..0xE] | 位置速度 | type 2 / type 3（乱数） |
| [0xF..0x11] | 回転速度 | type 0xB / 0xC |
| [0x12..0x14] | スケール速度 | type 0x12 / 0x13 |
| [0xA] / [0xB] | 追加スカラー（積分対象） | データ3 type 0x27 / 0x28 の +4 |

### `FUN_00403880` = 粒子の毎フレーム積分

```c
if (データ3 type 2 を持つ) pos    += posVel;
if (データ3 type 5 を持つ) rot    += rotVel;
if (データ3 type 8 を持つ) scale  += scaleVel;
if (データ3 type 0x27 を持つ) [0xA] += データ3-0x27[+4];
if (データ3 type 0x28 を持つ) [0xB] += データ3-0x28[+4];
age++;
```

→ **データ3 = 「積分の有効化」命令列。** 値そのものはデータ2 が供給する。

## 4. `FUN_00404750` = 放出・積分・寿命（パーティクル・プール本体）

```c
if (gen[5] < gen[6]) {                                   // 放出ウィンドウ（op 0x02 の param = 寿命）
  if (gen[5] % (frames+1) == 0) {                        // 周期 = gen チャンク +0x76（u16）+ 1
    for (i = 0; i <= (u8)(genBlock[0x78]); i++) {        // 放出個数 = genBlock+0x78 byte + 1
      粒子 = new(0x5C)（ゼロクリア）
      if (データ2 type 1)   pos    += rec[+4..+0xC];
      if (データ2 type 2)   posVel += rec[+4..+0xC];
      if (データ2 type 3)   posVel += rand/32768 * rec[+4..+0xC];
      if (データ2 type 9)   rot    += rec[+4..+0xC];
      if (データ2 type 0xA) rot    += rand * rec;
      if (データ2 type 0xB) rotVel += rec;  if (0xC) rotVel += rand * rec;
      if (データ2 type 0xF) scale  = rec;   if (0x10) scale += rand*rec;  if (0x11) 全軸 += rand*rec[+4];
      if (データ2 type 0x12) scaleVel += rec; if (0x13) scaleVel += rand*rec;
      if (データ2 type 7) {                                  // 球面散布
        dir = 単位球面乱数（rand 3 個 → 角度 → 回転ヘルパ）
        dist = (gen[7]+1) * unaff_EDI;                       // ★ 粒子番号 × EDI（2π/個数 と推定）
        pos    += dir * dist;
        posVel -= dir * unaff_EDI;                           // 速度は -dir × EDI
      }
      if (データ2 type 0x1F) {                               // ★ 円環配置（公式実装）
        ang = (gen[0x40A] * 6.283185) / (rec[+0x2C] + 1);    // ★ 位相 = 粒子通し番号 × 2π / (分割数+1)
        r    = rec[+0xC] * (1 + rand * rec[+8]?)             // 半径に乱数を乗算
        pos.x += cos(ang) * r;  pos.z += sin(ang) * r;       // ★ cos→x / sin→z（xz 平面）
        pos.y += rand * rec[+0x24] + rec[+0x20];
      }
      if (type 8 がある) speed = rec[+4];                    // 基底速度
      if (type 0x41 がある) speed += rand * rec[+4];
      if ((type 8 || type 0x41) && type 7 なし) {            // ★ 直進方向 = 初期位置から の正規化ベクトル
        d = normalize(pos - rec1.pos);  posVel += d * speed;
      }
      if (データ3 type 0x27) [0xA] = rec[+4];  if (0x28) [0xB] = rec[+4];
      if (データ2 type 0x1E の +4 がある) [0x15] = その値;    // 合成/属性フラグ？
      push 粒子; gen[0x409]++; gen[0x40A]++;
    }
  }
  gen[5]++;                                                 // 放出年齢
}
for each 粒子: FUN_00403880(gen, 粒子);                      // 積分
for each 粒子: if (age >= gen[7]) free + memmove で圧縮;     // 寿命 = データ2 type1 op の +0x22
```

**EffectTest の `--arrange ring` との差**: 本物は (a) 位相に**粒子通し番号 × 2π/(分割数+1)** を使う、
(b) cos→**x** / sin→**z**（xz 平面）、(c) 半径に乱数を乗算する、(d) y に base + 乱数を加算。

## 5. `FUN_004041b0` = ジェネレータ命令パーサ（VM#1 相当）

4 ストリームを順に走査（命令形式はタイムラインと同じ `op / len&0x1F / *4`）し、
**type ごとのテーブルに命令レコードのポインタを登録**：

| ストリーム | テーブル | 実装状況 |
|---|---|---|
| データ1 (gen+0x80) | ctx+0x1030 | **全 type 未実装**（`[1] typeXX Unknown. Doesnt work` のみ） |
| データ2 (gen+0x84) | ctx+0x1430 | 実装（下記） |
| データ3 (gen+0x88) | ctx+0x1830 | type 2/5/8/0x27/0x28 = 積分有効化 |
| データ4 (gen+0x8C) | ctx+0x1C30 | **全 type 未実装** |

データ2 の type と意味（放出時と描画時の両方から確定）:

| type | 意味 | 補足 |
|---|---|---|
| 1 | 初期位置 xyz（rec+4..+0xC）＋ **+0x20/+0x21 = 形状種別、+0x22 = 寿命（word）** | +0x21=0xB→0x1F ジオメトリ / 0xE→0x21 フレーム / 0x1D→0x25 を `ctx+0x102C` に生成。名前は rec+0xC の 4B |
| 2 / 3 | 位置速度 / 位置速度乱数 | |
| 7 | 球面散布（方向 + 距離） | |
| 8 / 0x41 | 速度スカラー / 速度乱数 | |
| 9 / 0xA | 回転初期値 / 乱数 | |
| 0xB / 0xC | 回転速度 / 乱数 | |
| 0xF / 0x10 / 0x11 | スケール初期 / 乱数 / 一様乱数 | |
| 0x12 / 0x13 | スケール速度 / 乱数 | |
| 0x1E | **合成フラグ**（rec+4: bit2 = DESTBLEND ONE/INVSRCALPHA） | 描画時に SetRenderState |
| 0x1F | **円環配置**（+0x2C = 分割数、+0xC = 半径…） | |
| 0x21..0x2F | **キートラック**（0x19 チャンク名 rec+8 → `ctx+0x2030+type*4` に 0x19+0x10 を保存） | 0x26 は評価結果を未使用 |
| 0x33..0x36 / 0x39 | キートラック（同上、用途未確定） | |
| 0x16 | テクスチャ色 rec+4（D3DCOLOR） | 描画時に BGR 入替 |

### データ2 type 0x21..0x2F = 粒子プロパティのキートラック（`FUN_004050b0`）

評価時刻 **t01 = age / (寿命+1)**（0..1 に正規化、`FUN_00403940` = 0..1 クランプ線形補間、終端 t==1.0）:

| type | 対象（粒子の初期値を上書き） |
|---|---|
| 0x21 / 0x22 / 0x23 | 位置 X / Y / Z |
| 0x24 / 0x25 | 回転 X / Y（0x26 は評価のみ） |
| 0x27 / 0x28 / 0x29 | スケール X / Y / Z |
| 0x2A / 0x2B / 0x2C / 0x2D | 色 R / G / B / α（byte 化して +8/+9/+0xA/+0xB に OR） |
| 0x2E / 0x2F | 幅 / 高さ（`ctx+0x102C` の +0xC/+0x10 に代入） |

## 6. 描画（`FUN_00405610` → `FUN_004050b0`）

### シーン全体（`FUN_00405ab0`）

- Clear: `0xFF2A172D`、Z=1.0、flags=3（TARGET|Z）
- View: eye=(0, 0.05, -5)、up=(0,-1,0)（`FUN_004131bc`）、Proj: fov=45°, aspect=4:3, near=1, far=20（`FUN_00411b22`）
- シーン既定: `SRCBLEND=SRCALPHA`、`DESTBLEND=INVSRCALPHA`、`CULLMODE=NONE`、`ZENABLE=3(Wバッファ)`、`SHADEMODE=1`
- TSS(0): COLOROP=MODULATE4X / ALPHAOP=SELECTARG1 / COLORARG1=DIFFUSE / COLORARG2=CONSTANT /
  ALPHAARG1=TEXTURE / ALPHAARG2=DIFFUSE / COLORARG0=TEXTURE / ALPHAARG0=TEXTURE

### ジェネレータごと（`FUN_00405610`）

BLENDOP=ADD / ALPHABLENDENABLE=1 / SRCBLEND=SRCALPHA / DESTBLEND=ONE を設定し、
**粒子ごとに** `ZWRITEENABLE=0`（0xE）＋ `ZENABLE=0`（0x07）＝**深度テスト完全 OFF** を設定して描画。
（旧ドキュメントの「0x0E=CULLMODE / 0x07=ZWRITEENABLE」は D3D8 定数の取り違えで、
正しくは **0x0E=ZWRITEENABLE / 0x07=ZENABLE**。CULLMODE=0x16 はシーンで NONE 済み）

### 粒子ごと（`FUN_004050b0`）

1. `t01 = age / (寿命+1)`、粒子位置/回転/スケールを基底値とし、キートラック (0x21..0x2F) で上書き
2. データ2 type 0x16 rec+4（D3DCOLOR）を BGR 入替で `ctx+0x102C` の +8 に
3. 色 R/G/B/α トラック (0x2A..0x2D) の評価値を byte 化して +8..+0xB に OR
4. 幅/高さ (0x2E/0x2F) → `ctx+0x102C` の +0xC/+0x10（VM スケールが基底、**0x21 実寸は掛けない** — §4.8 と整合）
5. 行列ヘルパで平行移動(0,0,0)+回転を合成し、デバイス呼び出しで描画

### 色（確定）

- 形状オブジェクト `+8` = D3DCOLOR (B,G,R,A)（`FUN_00402f20` は引数 XRGB を BGR 入替で +8 下 3 バイトへ）
- **デフォルト = `0x80808080`** → MODULATE4X (×4) で 0x80×4 = 1.0 = 中立
- キートラックの値は **0..255 バイト**で、0x80 が基準（半分で暗く、FF で ×4 明るく）
- α も同様に 0x80 が基準

## 7. EffectTest への反映指針（次段）

1. **パーティクル・プール**: `FUN_00404750` の仕様（周期放出 / 粒子 0x5C 構造 / 積分 / 寿命圧縮）を
   `Effect.cpp` に実装。円環は `--arrange ring` を公式仕様に差し替え（位相=通し番号×2π/(分割数+1)、cos→x/sin→z、半径乱数乗算）。
2. **データ3 = 積分有効化**の解釈を EffectTest の VM#2 に統合（速度を持つ op の実行条件）。
3. **キートラック**: type 0x21..0x2F の意味表を `EvalKeyTrack` に適用。t01 = age/(寿命+1)。
4. **色**: 頂点色デフォルト 0x80808080・0x80=中立・×4 モデルを `--colorop ffxi` の正式仕様に。
5. **合成**: シーン既定 INVSRCALPHA、`0x1E` bit2 で粒子単位に ONE/INVSRCALPHA 切替（`--blend auto` の正規実装）。
6. **深度**: 粒子は ZENABLE=0（テスト OFF）＋ZWRITE=0。
7. **op 0x0B**: 0x3D チャンク = サウンドキュー（非描画）。EffectTest では命令の可視化のみで可。

### 反映状況（2026-11-14 実装済み）

`--ffxivm on` で本パイプラインが有効になる。**既存 AltanaView 経路と並存**（既定 off、ゴールデン無変更）。

| 指針 | 実装 | ファイル |
|---|---|---|
| パーサ（§5） | `ParseFfxi()`: データ2/3 を `ffxiB`/`ffxiC` に type 付き保存（同 type は最後が有効）、キー op は 0x19 を **chunk+0x10** で `ffxiKeys` へ、type1 から寿命 +0x22 / 形状 +0x21 / 初期位置 +0x14..0x1C。形状名が linkName と異なれば 0x21/0x1F を追加解析 | `Effect.cpp` |
| 放出・積分・寿命（§3/§4） | `FfxiInitParticle`（type1→2→3→0xB→0xC→0x12→0x13→6→8/0x41→7→9→0xA→0xF/0x10/0x11→0x1F→方向ブロック→0x27/0x28 の順）/ `FfxiIntegrate`（データ3 type 2/5/8/0x27/0x28 ゲート）/ 寿命圧縮。放出条件 `emitAge < op02 param`、`emitAge % (genFrames+1) == 0` で `count+1` 個 | `EffectFfxi.cpp` |
| キー補間 | `EvalFfxiKey`（`FUN_00403940` 移植。t==1.0 が実キー、区間線形補間） | `EffectFfxi.cpp` |
| 描画同期（§6） | `FfxiSyncDraw`: t01=age/(life+1)、キー 0x21..0x2F で pos/rot/scl/色/幅高さを上書き、0x21 フレーム = floor(t01×枚数)、0x25 は `quadHalf = w×0.5` | `EffectFfxi.cpp` |
| 色チャネル | 0x2A=B / 0x2B=G / 0x2C=R / 0x2D=A（ARGB 消費）。基底色 = 0x16 rec+4（RGBA バイト順）、無ければ 0.5 中立。0x21 フレーム頂点色（0x80=1.0）を描画側で乗算 | `EffectFfxi.cpp` / `App.cpp` |
| 合成 | 0x1E rec+4 bit2 クリア = 加算（DESTBLEND ONE）。**粒子ごと**に `GfxSetBlendMode` 切替、終端で復元 | `App.cpp` |
| 深度 | `GfxSetDepthTest(false)`（DepthEnable=FALSE の第 2 DSS を追加）。RenderScene 冒頭で OFF / 終端で復帰 | `Gfx.cpp` / `App.cpp` |
| 方向 | `RandDir()` = YPR(乱数3角)×(1,0,0) 正規化。type6 は引数未読・単位方向加算、type7 は `dist=(life+1)×EffectSphDist()` | `EffectFfxi.cpp` |
| タイムライン接続 | op 0x02/0x03 の `param` を tlParam として `Spawn`→`Reset` に通し、`ffxiWindow`（放出ウィンドウ）に | `Effect.cpp` / `Effect.h` |

検証: `--ffxivm on --nodetypes ffxi --shot ROM\11\17.DAT`（Fire）で放出→積分→描画まで動作。
`--trace` の粒子列（g004: 寿命 35、scale 線形成長、色 0x3A808080 → (0.50,0.50,0.50,0.23)）は
データ値と一致。既定（非 ffxi）モードのゴールデンは無変更
（`11\17`=10,343/204、`11\18`=3,948/454、`12\43`=4,661/263、`--selftest` 40/40）。

## 8. 未確定（Unknowns）

- `unaff_EDI`（球面散布の距離係数）: x87 経由で逆コンパイルが欠落。2π/(放出個数) と推定（要動的検証）。
  → **EffectTest 側は定数で代替**（`EffectSphDist()`、既定 1.0）。type 7 の `dist = (life+1) × EDI`、
  `posV -= dir × EDI` の 2 箇所で使用。`--sphdist` での調整は未実装（`EffectSetSphDist()` は公開済み）。
- データ2 type 0x1F 円環レコードの全引数（+4/+8/+0x10/+0x14/+0x20/+0x24 の役割分担）。
  → **移植時に仮確定**（`EffectFfxi.cpp` `FfxiInitParticle`）:
  +4=半径の乱数幅 / +8=半径の基準 / +0x0C=x 方向の倍率 / +0x14=z 方向の倍率 /
  +0x20=y の基準 / +0x24=y の乱数幅 / +0x2C=位相分割数（位相 = spawnedTotal×2π/(div+1)）。実データでの検証は未。
- データ2 type 0x33..0x36 / 0x39 キートラックの意味。
- データ1 / データ4 ストリーム: ffxitool5 も未実装。AltanaView の VM#1/VM#2 が担当（既存解析のまま）。
- ~~0x19 キー表の先頭オフセット~~ → **✅ 確定（2026-11-14 実データ検証）**:
  `ROM\11\17.DAT`（Fire）の 0x19 チャンク 8 個（k000..k007）を機械判定した結果、
  **開始 = chunk+0x10、`{t,v}` 8 バイト単位、t==1.0 ペアは実キー（終端値を持つ）**。
  全チャンクで +0x10 のみ「(0,v0) 開始・t 単調増加・末尾 t==1.0」が成立
  （例 k006: `(0,0)(0.25,0.49)(0.516,0.49)(1,-0.01)`、k005: `(0,0)(1,1.66)`)。
  +0x18 は偶然成立するが先頭 `(0,0)` キーと最初の区間が欠落する → **EffectTest の +0x18 は誤り、+0x10 に修正**。
  チャンク先頭 0x00..0x0F は name(4B)+type|size(4B)+zero(8B)。
  ※ 実装は **ffxi 経路（`FfxiKey.first`）のみ +0x10**。既存 AltanaView 経路（`keyFirst`）は
  ゴールデン維持のため +0x18 のまま（`Effect.h` SubUnit のコメント参照）。
- 形状オブジェクト 0x25 由来（+0x21==0x1D）の描画内容（vtable PTR_FUN_004301ec）。
  → 幅・高さ（キー 0x2E/0x2F とデータ3 0x27/0x28 の積算値）を `quadHalf = w×0.5 / h×0.5` として
  ビルボード描画した近似を実装（`FfxiSyncDraw`）。形状オブジェクト本来の描画経路は未解析。
- `FUN_00403030`（0x21 スプライト描画）は vtable 経由で呼ばれるため静的呼び出しグラフに現れない。
  ~~フレーム選択（`fStack_10`）の乱数/時刻依存は未確定~~ →
  **✅ 確定（2026-11-14）**: `while (i/frames < t01) i++` のとおり **t01 = age/(life+1) 駆動**。
  idx = floor(t01×枚数)。乱数ではない。
- **D3DX 相当の内部実装テーブル（2026-11-14 追加確定）**: ffxitool5 は d3dx8 を静的リンクせず、
  独自実装を持ち、起動時に `FUN_00413c68` が src `0x43b278`（0x39 dword）→ dst `0x43b190` へ退避コピーする。
  関数ポインタは `0x43b190` 台のテーブル経由で呼ばれる（thunk `jmp [0x43bxxx]`）。同定結果:

  | ffxitool5 | 相当（D3DX8） | 根拠 |
  |---|---|---|
  | `FUN_004119ca` | `D3DXMatrixRotationYawPitchRoll` | `0x43b230` → 内部で cos/sin 3 組 + 行列 12 float |
  | `FUN_0041061d` | `D3DXVec3TransformCoord` | `0x43b1b8` → `FUN_00412a24` = 4x4 行列変換（w 除算付き） |
  | `FUN_0041039e` | `D3DXVec3Normalize` | `0x43b1ac` |
  | `FUN_004114f0` | `D3DXMatrixTranslation` | 単位行列の +0xC..+0x14 に xyz |
  | `FUN_00411580` | `D3DXMatrixScaling` | 対角に xyz |
  | `FUN_004115fe / 1169a / 11737` | `D3DXMatrixRotationX / Y / Z` | cos/sin の配置パターン |

  これにより type 6 / type 7 の方向は **YPR(乱数3角) 行列 × (1,0,0) を正規化した単位球面乱数**
  と確定し、`RandDir()` として移植した。
  ※ type 6 はレコードの引数を読まない（単位方向をそのまま `pos +=`。乱数 1 回破棄 + 3 角消費）。
  ※ type 7 の `unaff_EDI` だけが残存する未確定（上記参照）。
- **回転の写像（2026-11-14 追加）**: キー 0x24（rotX）のみビルボードのロールへ写像。
  0x25（rotY）/ 0x26（rotZ）は評価のみで捨てる（ffxitool5 も 3D ビルボード描画では未使用）。
  初期回転は type 9/0xA（`rot`）、回転速度は type 0xB/0xC（`rotV`）で積分。
- **色チャネル対応（2026-11-14 確定）**: 逆コンパイル `FUN_00402e60/02e90/02ec0/02ef0` の
  書き込みバイト位置（+0xB/+8/+9/+0xA）から、キー 0x2A=B / 0x2B=G / 0x2C=R / 0x2D=A。
  色オブジェクト（+8 dword）は **byte0=B / 1=G / 2=R / 3=A** の並び（D3DCOLOR ARGB 消費のため）。
  0x16 の rec+4 dword は RGBA バイト順（byte0=R）で、`FUN_00402f20` が BGRA に並べ替える。
- **param=0 のジェネレータ（Fire の g006/gs00）** は放出ウィンドウ（op 0x02 の param）が 0 のため
  放出されない。忠実な挙動としてそのまま実装。
- **base DAT 由来ジェネレータ（`ROM\0\0.DAT` の 0x05）** は BaseGenerator 構造が別のため
  `ParseFfxi` 未対応 → ffxi モードでは粒子が出ない（許容。既存 AltanaView 経路で描かれる）。
