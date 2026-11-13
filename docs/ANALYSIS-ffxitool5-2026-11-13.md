# ffxitool5.exe（FFXITool5 エフェクトビュア）解析 — 2026-11-13

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

## 5. `FUN_004050b0`（1 インスタンス描画）の構造

- 冒頭で行列スタックを組み立て、`thunk_FUN_0041154c` / `0x4114bc` / `0x410c10` /
  `0x4115dc` / `0x411678` / `0x411715` を呼ぶ（**自前の行列・回転ヘルパ**）。
- 末尾で `(**(code**)(*DAT_0043f3c0 + 0x94))(...)` = **Clear**、
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
