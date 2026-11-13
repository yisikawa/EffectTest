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
  デバイス生成 → 以降は vtable 呼び出しなので、静的 xref では追いにくい。
- `>>3 & 0x7FFFF0`（AltanaView のチャンク走査定数）に**一致するバイト列は無い**。
  → ffxitool5 は DAT のチャンク解釈が AltanaView と別実装（または別経路）。
  ここは次段で `FUN_004012e0` の呼び出し元から辿る。

---

## 5. 次段（このアプリを優先して進める）

1. `FUN_004012e0` の呼び出し元（4 箇所）から **DAT ローダ → エフェクト構築**を辿る
2. 0x401769 を含む初期化関数から**デバイスポインタのグローバル**を特定し、
   その xref から**描画関数（SetRenderState / DrawPrimitive）**を特定
3. 得られた知見を EffectTest に反映:
   - α・色・合成（D3D8 の固定機能設定は読みやすい）
   - 粒子サイズの決め方（Scale の掛け方）
   - Move の積分（AltanaView で仮説済み。ここで裏取りできる）
4. **参照画像の自動収集**：ID を順に送ってタイトルから DAT を記録し、各エフェクトの
   画面を撮る。AltanaView のツリー操作より格段に安定する。

---

## 6. 再現手順（参照の取り方）

```
# ffxitool5 を必ず自分のフォルダで起動（.\effect.txt を相対で読むため）
Start-Process 'D:\tool\ffxitool13032900\ffxitool5.exe' -WorkingDirectory 'D:\tool\ffxitool13032900'

# NumPad- (VK 0x6D) を送って ID を 1 つ戻す。タイトルで ID と DAT を確認
# Space で再生し直す
```

スクリプト: `%TEMP%\av_cure\ffxitool_nav.ps1`
