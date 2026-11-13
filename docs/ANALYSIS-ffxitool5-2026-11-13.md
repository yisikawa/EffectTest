# ffxitool5.exe（FFXITool5 エフェクトビュア）解析 — 2026-11-13
> **重要（2026-11-13 追記）**: 本アプリは **Cure のノードの多くを未実装**で、
> 画面に `Unknown. Doesn't work` と表示する。描画ステート・DAT 形式・キー補間の
> **仕様の出典としては有効**だが、**見た目の正解としては使えない**
> （Cure の見た目の参照は AltanaView）。詳細は §8。



`D:\tool\ffxitool13032900\ffxitool5.exe`
AltanaView（UPX + D3D9 + 巨大）より**はるかに解析が容易**な参照実装。
本書は判明した事実と、EffectTest 側に流用できる点の記録。

---

## 0. 結論（先に）

1. **Cure = `ROM\10\9.DAT` が実機で確定**（`[2801] ROM\10\9.DAT`）。
   我々の `10/9 = Cure` の同定は正しかった。
2. **ID → DAT の解決式が判明**（逆コンパイルで確定）。
3. 本アプリは **MSVC 製・非パック・D3D8・固定イメージベース 0x400000**。
   Ghidra で関数がそのまま読める（AltanaView のような UPX 展開が不要）。
4. PDB 名は **`Palladium.pdb`**（GUID 230c4d65-…, age 0x28）。PDB 本体は無い。

---

## 1. バイナリ素性

| 項目 | 値 | 出典 |
|---|---|---|
| 形式 | PE32 / x86 / 270,336 B | `inspect_pe_imports` |
| SHA-256 | `2b159dc0dca24c460e5ebdfe2bf3f7320631612a8ed7c7aac699c182efd27f69` | 同上 |
| パック | **無し**（UPX 文字列なし） | 文字列走査 |
| 言語 | Delphi/VB ではない（MSVBVM/Borland なし） | 同上 |
| DllCharacteristics | DYNAMIC_BASE **無効**（固定ベース） | `inspect_binary_hardening` |
| 主要 import | `d3d8.dll!Direct3DCreate8`、`DSOUND.dll`、`WINMM!timeGetTime`、`USER32`(CreateDIBSection 他) | `inspect_pe_imports` |
| DEBUG ディレクトリ | 有り → `Palladium.pdb` | Ghidra プログラム情報 |

D3D8 である点が重要：AltanaView の D3D9 ステート（MODULATE2X/4X 等）とは
別経路で、**固定機能パイプラインの素直な設定**になっている可能性が高い。

---

## 2. ID → DAT パスの解決（逆コンパイルで確定）

### 2.1 パス書式（`.rdata`）

```
0x42fff0  "%sROM%d\FTABLE%d.DAT"
0x430008  "FTABLE.DAT"
0x430014  "%sROM%d\VTABLE%d.DAT"
0x43002c  "VTABLE.DAT"
0x430038  "%sROM%d\%d\%d.DAT"
0x43004c  "%sROM\%d\%d.DAT"
0x430400  "EFFECT INFO"
0x43040c  ".\effect.txt"
```

### 2.2 解決関数 `FUN_004012e0`（0x4012e0..0x40135c, 125 B）

```c
uVar1 = FUN_00401280(id);            // 1～5 回のテーブル検索（FUN_004010d0 の再試行）
if (uVar1 == 0) return 0;
uVar2 = (ushort)(uVar1 >> 16);       // rom 番号
if (uVar2 == 1)
    wsprintfA(p, "%sROM\%d\%d.DAT", base, (uVar1 & 0xffff) >> 7, uVar1 & 0x7f);
if (1 < uVar2)
    wsprintfA(p, "%sROM%d\%d\%d.DAT", base, uVar1 >> 16, (uVar1 & 0xffff) >> 7, uVar1 & 0x7f);
```

→ **パック値のビット配置**（EffectTest の `ListDb::ResolvePath` と同じ規則）:

```
lo  = packed        & 0x7F
hi  = (packed >> 7) & 0x1FF
rom =  packed >> 16
path = rom == 1 ? "ROM\hi\lo.DAT" : "ROM<rom>\hi\lo.DAT"
```

### 2.3 `effect.txt` の読み込み `FUN_00406350`（0x406350..0x40671f, 976 B）

`.\effect.txt` を `"EFFECT INFO"` ヘッダ付きで読み、`ID,名前` 行を索引化する。
`//白魔法` などのコメント行でカテゴリを切り替える。

---

## 3. 実機で取得した ID → DAT 表（白魔法）

ウィンドウタイトルが `FFXITool5 <名前> [<ID>] ROM\<hi>\<lo>.DAT Ver.8.02.05.0`
の形式なので、**タイトルを読むだけで ID と DAT の対応が取れる**。
NumPad− を 1 回送るごとに ID が 1 減り、次のように連続していた:

```
ケアル     [2801] ROM\10\9.DAT      ← Cure（今回の対象）
ケアルII   [2802] ROM\10\10.DAT
ケアルIII  [2803] ROM\10\11.DAT
ケアルIV   [2804] ROM\10\12.DAT      ← Cure IV
ケアルV    [2805] ROM\10\13.DAT
ケアルVI   [2806] ROM\10\14.DAT
ケアルガ   [2807] ROM\10\15.DAT
…
ケアルガV  [2811] ROM\10\19.DAT
レイズ     [2812] ROM\10\20.DAT
…
ホーリー   [2821] ROM\10\29.DAT
…
ディア     [2823] ROM\10\31.DAT
…
プロテス   [2843] ROM\10\51.DAT
プロテスV  [2847] ROM\10\55.DAT
（初期表示） [2848] ROM\10\56.DAT
[2800] は "miss"（ROM\10\8.DAT）
```

**白魔法は閉じた式**（実測から）:

```
hi = 10,  lo = id - 2792        （2801 → 9、2804 → 12、2848 → 56 で検証済み）
```

> EffectTest の `--shot` に `--id 2801` のような入力を用意すれば、
> 名前から DAT を引けるようになる（次段の改善案）。

---

## 4. 描画側の手がかり

- `Direct3DCreate8` の呼び出しは **0x401769**（初期化関数内）。
- `>>3 & 0x7FFFF0`（AltanaView のチャンク走査定数）に一致するバイト列は無く、
  `and eax,0x7FFFF0` も存在しない → チャンク解釈は AltanaView と別実装。

### 4.1 読み込み連鎖（確定）

```
FUN_00405e30  (0x405e30..0x406118, 745 B)  ← エフェクト選択（キー/一覧から）
  ├─ FUN_00401440 (0x401440..0x401493,  84 B)  DAT を開く
  │    ├─ FUN_004012e0  ID → "ROM\<hi>\<lo>.DAT"
  │    └─ FUN_00401360 (0x401360..0x40143f)  ファイル全体を ReadFile で読む
  │         CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, 0x20)
  │         GetFileSize → operator_new → ReadFile → GetFileTime → SystemTime
  ├─ FUN_00402df0 (0x402df0..)  effect.txt の 1 行を登録
  │    エントリ = 21 dword (0x15) 間隔。+0x4413d8 に 4 dword、+0x4413e9 に
  │    FUN_00401580 の戻り値（名前オブジェクト）。DAT_0049e420 が件数。
  ├─ FUN_00403ac0  ← **次段の本命**（パーサ or 描画）
  └─ FUN_00405990 / 0x406720 / 0x406725 / 0x406778
```

### 4.3 チャンク走査（AltanaView と同一式であることを確認）

`FUN_00403ac0`（0x403ac0.., this-call, `(this, int* cursor, u32 name)`）:

```c
buf  = p[0x45];  size = p[0x46];
next = (buf[1] >> 7) & 0x7FFFF;               // ← 単位は 16 バイト
if (next == 0 || size < (next << 4) || (buf[1] & 0x7F) != 1) return 0;
name4 = buf[0];  hdr = buf[1];
for (;;) {
  if ((hdr & 0x7F) == 7 && name4 == param2)   // type 7 = main、名前一致
      return FUN_00403a70(this, chunk);
  next = (*(u32*)(cursor + 4) >> 7) & 0x7FFFF;
  if (next == 0) return 0;
  cursor = (u8*)cursor + next * 0x10;         // チャンクサイズ = next * 16
  ...
}
```

`(hdr >> 7) & 0x7FFFF) << 4` は **AltanaView の `(hdr >> 3) & 0x7FFFF0` と同値**。
→ **DAT のチャンク規則は両者で同一**（先の「別実装」は誤り。バイト列検索が
`shr 7 / and / shl 4` に分割されていたため見つからなかっただけ）。

### 4.4 main チャンクのヘッダ（`FUN_00403a70`）

```c
*(int*)this        = main;                    // main チャンク先頭
*(this + 0x04) = *(u32*)(main + 0x20) + main; // ストリーム A（相対オフセット）
*(this + 0x08) = *(u32*)(main + 0x24) + main; // ストリーム B
*(this + 0x0C) = *(u32*)(main + 0x28) + main; // ストリーム C
*(this + 0x14) = *(u32*)(main + 0x2C);        // 長さ（フレーム数/継続時間）
*(this + 0x10) = 0;
*(this + 0x0C28) = 0;  *(this + 0x41C) = 0;
*(this + 0x820)  = 0;  *(this + 0x0C24) = 0;
```

**重要**: ffxitool5 は main チャンクの **+0x20 / +0x24 / +0x28** を
「3 本のストリームの相対オフセット」、**+0x2C** を長さとして読む。

EffectTest 側は現在 `main + 0x50`（タイムライン）と `main + 0x8C`（ストリーム D）を
使っており、**オフセットの対応関係を再確認する必要がある**（次段の最優先）。

エフェクトオブジェクトは少なくとも **0xC2C バイト**で、
`+0x41C` と `+0x820`（差 0x404 = 1028 B）にジェネレータ別の状態を持つ。

### 4.2 ローダ・オブジェクトのレイアウト（`FUN_00401440` の this）

| オフセット | 内容 |
|---|---|
| +0x000 | ロード済みフラグ（0 初期化） |
| +0x004 | パス文字列（`char[0x100]`） |
| +0x104 | FILETIME → SYSTEMTIME（8 ワード） |
| +0x114 | DAT 全体のバッファ（`operator_new`、`_free` で解放） |
| +0x118 | ファイルサイズ |



---

### 4.5 描画ステート（`FUN_00405610` / 0x405610..0x40579d, 398 B）

デバイスはグローバル **`DAT_0043f3c0`**（`FUN_004050b0` の `+0x94` = Clear 呼び出しで確定）。

```c
SetRenderState(0xAB /*BLENDOP*/,        1   /*ADD*/);
SetRenderState(0x1B /*ALPHABLENDENABLE*/, 1);
SetRenderState(0x13 /*SRCBLEND*/,       5   /*SRCALPHA*/);
SetRenderState(0x14 /*DESTBLEND*/,      2   /*ONE*/);     // ← 加算合成
SetTextureStageState(0, 1 /*COLOROP*/,   6 /*MODULATE4X*/);
SetTextureStageState(0, 2 /*ALPHAOP*/,   2 /*SELECTARG1*/);
SetTextureStageState(0, 3 /*COLORARG1*/, 0 /*DIFFUSE*/);
SetTextureStageState(0, 4 /*COLORARG2*/, 6 /*CONSTANT*/);
SetTextureStageState(0, 5 /*ALPHAARG1*/, 2 /*TEXTURE*/);
SetTextureStageState(0, 6 /*ALPHAARG2*/, 0 /*DIFFUSE*/);
SetTextureStageState(0, 0x10 /*COLORARG0*/, 2 /*TEXTURE*/);
SetTextureStageState(0, 0x11 /*ALPHAARG0*/, 2 /*TEXTURE*/);

for (i = 0; i < param_1[0x409]; ++i) {          // インスタンス数
    SetRenderState(0xAB, 1);
    SetRenderState(0x13, 5);                     // SRCALPHA
    SetRenderState(0x14, 2);                     // ONE
    SetRenderState(0x0E /*CULLMODE*/, 0);        // NONE（両面）
    SetRenderState(0x07 /*ZWRITEENABLE*/, 0);    // 深度は書かない
    FUN_004050b0(param_1, param_1[9 + i]);       // 1 インスタンスを描く
}
```

**EffectTest との比較**

| 項目 | ffxitool5（このアプリ） | AltanaView（D3D9 実測） | EffectTest 現行 |
|---|---|---|---|
| 合成 | SRCALPHA + **ONE**（加算） | SRCALPHA + ONE | 既定 alpha（=SRCALPHA/INVSRCALPHA） |
| COLOROP | **MODULATE4X（RGB ×4）** | MODULATE2X（×2）+ α は MODULATE4X | 自前 PS で ×gain(1.5) |
| ALPHAOP | **SELECTARG1（テクスチャ α）** | MODULATE4X（α ×4） | 自前 PS |
| CULL | NONE | NONE | — |
| ZWRITE | 0 | 0 | — |

→ 「白飛びする明るさ」の正体は **RGB ×4 の MODULATE4X + 加算合成**。
AltanaView はステージを 2 段（×2 と ×4）に分けていたが、ffxitool5 は 1 段で ×4。
**どちらも「×4 相当」**で、EffectTest の `--gain 1.5` では足りないことが裏付けられた。

### 4.6 エフェクト・オブジェクトのレイアウト（`FUN_00405990` = 破棄処理から）

`FUN_00405990` は再帰的に子を解放する破棄関数で、配列と件数の位置が判明した。

| オフセット | 内容 | 件数の位置 |
|---|---|---|
| +0x004 / +0x008 / +0x00C | main の 3 ストリーム | — |
| +0x014 | 長さ（フレーム数/継続時間） | — |
| +0x01C | 子オブジェクトのポインタ配列（0x400 B） | **+0x41C** |
| +0x420 | サブエフェクトのポインタ配列（0x400 B） | **+0x820** |
| +0x824 | 描画オブジェクト配列（各 +0x18 に D3D リソース） | **+0xC24** |
| +0xC28 | （予備カウンタ） | — |
| +0x102C | 頂点/インデックスバッファ相当（`FUN_00402f20` / `FUN_00402e60`、破棄は vtable+8） | — |

- 0x41C → 0x820 → 0xC24 が **0x404 間隔**（= 0x400 の配列 + 4 の件数）。
- 子は `FUN_00403a00` で解放、各描画オブジェクトは `+0x18` の D3D リソースを
  `vtable+8`（Release）してから `_free`。

**EffectTest との対応（推定）**

| ffxitool5 | EffectTest |
|---|---|
| +0x41C 子オブジェクト配列 | `MotionInstance::subs`（`SubUnit`） |
| +0x824 描画オブジェクト（+0x18 にテクスチャ） | `MotionInstance::srv` / `texIndex` |
| +0x102C 頂点バッファ | 毎フレーム生成する頂点配列（`GfxDrawQuad`） |

---

### 4.7 色の受け渡し（`FUN_00402f20` / `FUN_00402e60`）

描画オブジェクト `this+0x102C` は **1 個の D3DCOLOR を +8 に持つ**。

```c
// FUN_00402f20: RGB を BGR 順に詰め替えて +8 の下 3 バイトへ
*(u32*)(this+8) = (p>>16 & 0xFF) | ((p & 0xFF) << 16) | (p & 0xFF00)
                | (*(u32*)(this+8) & 0xFF000000);

// FUN_00402e60: α を +0xB に書き、+8 の最上位バイトへ
*(u8*)(param+0xB) = 0;
u = FUN_00406848();                       // 供給元（次段で特定）
*(u32*)(param+8) = *(u32*)(param+8) | ((int)u << 0x18);
```

→ ffxitool5 は **粒子色を D3DCOLOR（ARGB）にパック**し、
`DrawPrimitive` の頂点色として渡す（固定機能の DIFFUSE）。

### 4.8 クワッドのサイズ（`FUN_004050b0` 中ほど）

```c
fStack_228 = (float)param_1[0xb];            // 高さ（インスタンス +0x2C）
fStack_20c = (float)param_1[10];             // 幅  （インスタンス +0x28）
if (track 0x14e8) fStack_20c = FUN_00403940(*(float**)(this+0x20e8), t);  // 幅トラック
if (track 0x14ec) fStack_228 = FUN_00403940(*(float**)(this+0x20ec), t);  // 高さトラック
if (*(int*)(this+0x102c)) {
    *(float*)(*(int*)(this+0x102c) + 0x0C) = fStack_20c;   // 幅
    *(float*)(*(int*)(this+0x102c) + 0x10) = fStack_228;   // 高さ
}
/* このあと行列（平行移動・回転）を組んで頂点を生成 */
```

**結論: サイズは VM の Scale そのもの**（`param_1[10]` = Scale X、`param_1[0xb]` = Scale Y）。
`0x21` のクワッド実寸（±2 など）は**掛けない**。

→ EffectTest は **`--size scale`**（`halfW = scale[0]`, `halfH = scale[1]`）が正解。
Cure の prn は `Scale = (0.1, 0.1, 1)` → **0.2 m 角**で、参照の星（833×709 / dist 2.785 で
30〜40 px）と一致する。`--size quad`（0x21 実寸）や `--size meter`（1 m 固定）は大きすぎる。

## 5. `FUN_004050b0`（1 インスタンス描画）の構造

### 5.1 毎フレームの値の決め方


- 冒頭で行列スタックを組み立て、`thunk_FUN_0041154c` / `0x4114bc` / `0x410c10` /
  `0x4115dc` / `0x411678` / `0x411715` を呼ぶ（**自前の行列・回転ヘルパ**）。
- 粒子ごとに `this + 0x1488 .. +0x14d4` の**トラック表**と
  `this + 0x2088 .. +0x20d4` の**値ポインタ**を見て、
  `FUN_00403940(ptr, t)` で**時刻 t のキー値を評価**しローカルへ入れる。

```c
if (*(int*)(this+0x14b4) && *(float**)(this+0x20b4)) local_220 = FUN_00403940(...);
if (*(int*)(this+0x14b8) && *(float**)(this+0x20b8)) local_224 = FUN_00403940(...);
if (*(int*)(this+0x14bc) && *(float**)(this+0x20bc)) local_214 = FUN_00403940(...);
fStack_208 = param_1[4];  fStack_210 = param_1[5];          // 基底値
if (*(int*)(this+0x14c0) ...) fStack_208 = FUN_00403940(...);
if (*(int*)(this+0x14c4) ...) fStack_210 = FUN_00403940(...);
fStack_218 = param_1[7];  fStack_204 = param_1[8];  fStack_21c = param_1[9];
if (*(int*)(this+0x14cc) ...) fStack_218 = ...;
if (*(int*)(this+0x14d0) ...) fStack_204 = ...;
if (*(int*)(this+0x14d4) ...) fStack_21c = ...;
if (*(int*)(this+0x1488) && *(void**)(this+0x102c)) {
    FUN_00402f20(*(void**)(this+0x102c), *(u32*)(*(int*)(this+0x1488)+4));  // テクスチャ/UV
    FUN_00402e60(*(int*)(this+0x102c));                                     // バッファ確定
}
```

- `FUN_00403940(track, t)` = **キーフレーム評価**（EffectTest の `EvalKeyTrack` に相当）。
- `param_1` = インスタンス状態の配列。**`[4] [5]`** が第 1 群、
  **`[7] [8] [9]`** が第 2 群（位置 xyz / 色 RGB のいずれか）。トラックがあれば上書き。

### 5.2 ブレンドはインスタンスごとに切り替わる

```c
if ((uVar1 & 4) == 0)  SetRenderState(0x14 /*DESTBLEND*/, 2 /*ONE*/);          // 加算
else                   SetRenderState(0x14 /*DESTBLEND*/, 6 /*INVSRCALPHA*/);  // α
```

→ **フラグの bit2（値 4）で「加算 / α」を選ぶ**。
AltanaView の「テクスチャの黒割合で加算を自動判定」に相当する仕組みが、
ffxitool5 では**明示フラグ**。EffectTest の `--blend auto`（blackRatio）はその近似として妥当。

### 5.4 キーフレーム評価 `FUN_00403940`（0x403940）

```c
float FUN_00403940(float* p, float t)     // p = {t0,v0,t1,v1,...} 末尾に t==1.0
{
    if (t > 1.0f) t = 1.0f;  else if (t < 0.0f) t = 0.0f;   // 0..1 にクランプ
    v = p[1];  cur = p[0];
    while (cur != 1.0f) {                 // ★ 1.0 が終端マーカー
        next_t = p[2];
        if (t == cur) break;
        if (cur <= t && t <= next_t)
            return (t - cur) * (p[3] - p[1]) / (next_t - cur) + p[1];   // 線形補間
        v = p[3];  p += 2;  cur = next_t;
    }
    return v;
}
```

**EffectTest との違い（重要）**

| 項目 | ffxitool5 | EffectTest 現行 |
|---|---|---|
| 表の形 | `{t,v}` の**平坦な配列**、**末尾に t=1.0 の終端** | 0x19 チャンクを `[time, value]` として直接読む |
| 時間 | **0..1 に正規化**（クランプ） | `T01()` をそのまま使用 |
| 補間 | **単純な線形**（毎フレーム、キー区間を探す） | カーソル前進＋現在値基準の増分補間（`EvalKeyTrackIncr`） |
| キー数 | 終端マーカーまで走査 | `(size-0x18)/8` |

→ ffxitool5 は **0x19 を「正規化済みの平坦配列」に前処理**してから線形補間する。
EffectTest の「カーソル前進型」は ffxitool5 とは別方式なので、
**α が 0 付近に張り付く現象（prn の 0.006〜0.09）はここで説明できる可能性が高い**。
次段で「0x19 → 平坦配列＋終端 1.0」への変換関数を特定し、比較する。

### 5.5 末尾

`(**(code**)(*DAT_0043f3c0 + 0x94))(...)` = **Clear**、
その後 `(**(code**)(*(this+0x102c) + 8))(...)` = Release 相当。



## 6. 次段（このアプリを優先して進める）

1. ~~`FUN_00403ac0` を解読~~ **完了**（main チャンクの特定と 3 ストリームの取得）。
   次は `FUN_00403a70` の戻り値（this+0x04/0x08/0x0C のストリーム）を
   **誰が毎フレーム走査するか**＝タイムライン実行関数を特定する。
2. **main チャンクの +0x20/+0x24/+0x28 と +0x2C の意味を確定**し、
   EffectTest の `+0x50`（タイムライン）/`+0x8C`（ストリーム D）と対応づける。
   → Cure の 9 命令タイムラインがどれに当たるかを実データで照合できる。
3. ~~デバイスグローバルと描画ステート~~ **完了**（`DAT_0043f3c0`、§4.5）。
   次は `FUN_004050b0` の**頂点生成部**（サイズ・UV・α の決め方）を読む。
4. 得られた知見を EffectTest に反映:
   - **加算合成を既定に**（SRCALPHA + ONE）
   - **RGB ×4**（MODULATE4X 相当）に合わせる
   - 粒子サイズの掛け方（`FUN_004050b0` から）
   - Move 積分の裏取り
5. **参照画像の自動収集**：ID を順に送ってタイトルから DAT を記録し各エフェクトを撮影。

---

## 7. 再現手順（参照の取り方）


```
# ffxitool5 を必ず自分のフォルダで起動（.\effect.txt を相対で読むため）
Start-Process 'D:\tool\ffxitool13032900\ffxitool5.exe' -WorkingDirectory 'D:\tool\ffxitool13032900'

# NumPad- (VK 0x6D) を送って ID を 1 つ戻す。タイトルで ID と DAT を確認
# Space で再生し直す
```

スクリプト: `%TEMP%\av_cure\ffxitool_nav.ps1`

## 8. 実機の表示（Cure 2801）— 本アプリは部分実装

`FFXITool5 ケアル [2801] ROM\10\9.DAT` の画面（`docs/shots/2026-11-13-cure-ffxitool5-ref.png`）は
左半分が**診断ログ**で、そこにこう出る:

```
kr1-1_1 type:1 Unknown. Doesn't work
kr1-1_2 type:1 Unknown. Doesn't work
kr1-1_3 type:0xE Unknown. Doesn't work
pn1-1  type:1 Unknown. Doesn't work
pn1-1  not found ID:ring 44 24 -- 16896.0
pn1-2  type:2 Unknown. Doesn't work
pn1-3  type:0x38 Unknown. Doesn't work
pn1-3  type:0x3E Unknown. Doesn't work
kr3-1_1 type:1 ... kr3-1_2 type:0xE ...
```

- **ノード種別（type）の大半が未対応**で、アプリ自身が `Doesn't work` と明示している
- `not found ID: ring 44 24 -- 16896.0` = リンク名 `ring` と ID 44/24 の解決失敗
- 実際に描かれるのは **白い星 1〜2 個と細い縦線、小さな点**のみ

→ **ffxitool5 は「仕様の出典」としては優秀だが「見た目の正解」ではない。**
   - 使える: ID→DAT、チャンク規則、main の 3 ストリーム、描画ステート、
     キー補間、サイズ（VM Scale）、色のパック
   - 使えない: Cure の見た目（未実装ノードが多い）

**したがって見た目の参照は AltanaView のまま**（`main` 側の解析）。
ffxitool5 で得た「加算 / RGB×4 / サイズ=Scale / キー=0..1 正規化 / 発生=間隔×寿命」は
AltanaView の観測（白い四芒星・紫の光）と矛盾しないため、そのまま採用してよい。
### 8.1 「動いているもの」だけを参考にする方針

実機ログで `Doesn''t work` が出る type は **1 / 2 / 0xE / 0x38 / 0x3E**。
これらは ffxitool5 が未実装なので**参照にしない**。

実際に描画されているもの（＝参考にしてよい）:

| 画面の要素 | 対応 | 我々の実装 |
|---|---|---|
| 白い四芒星（十字の光芒） | 対応済みノード | ✅ 一致（`--ffxi on` で再現） |
| 細い縦の光 | 対応済みノード | ○ 出る |
| 小さな点 | 対応済みノード | ○ 出る |

→ **「白い四芒星が出る」という一点で ffxitool5 と一致**しているので、
そこを基準に位置・サイズ・明るさを合わせる。未対応 type の見た目は
**AltanaView の参照**（紫の光・散在）で補う。