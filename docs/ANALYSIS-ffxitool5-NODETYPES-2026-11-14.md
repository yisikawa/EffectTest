# ffxitool5 のデータ1〜4 type 対応表（`Doesnt work` の範囲）— 2026-11-14

> 目的: ffxitool5.exe の「`Doesnt work`」が**どのデータストリームの、どの type** を指すかを
> バイナリから確定し、**対象外**の範囲を明文化する。
> 対象外の type は EffectTest でも実装・評価の対象にしない（`--nodetypes ffxi` で実行を止める）。

対象: `D:\tools\ffxitool13032900\ffxitool5.exe`
（SHA-256 `2b159dc0dca24c460e5ebdfe2bf3f7320631612a8ed7c7aac699c182efd27f69` /
PE32 x86 / 非パック / D3D8 / 固定ベース 0x400000。
`docs/ANALYSIS-ffxitool5-2026-11-13.md` と同一バイナリ）

---

## 0. 結論

1. `Doesnt work` の実体は **4 本の文字列**で、いずれも「データ1〜4 の type ディスパッチの
   default 節」から出る。つまり **type 単位の未実装通知**である。

   ```
   0x430360  "%4.4s:[1] type%02X Unknown. Doesnt work\r\n"
   0x4302a8  "%4.4s:[2] type%02X Unknown. Doesnt work\r\n"
   0x43027c  "%4.4s:[3] type%02X Unknown. Doesnt work\r\n"
   0x430250  "%4.4s:[4] type%02X Unknown. Doesnt work\r\n"
   ```

2. 参照元はすべて **1 関数** `FUN_004041b0`（0x4041b0〜0x4045fd, 1,099 B / __thiscall）。
   先頭で main チャンクの `+0x80/+0x84/+0x88/+0x8C` を 4 本のストリームとして取り込む:

   ```c
   *(int *)(this + 0x04) = *(int *)(main + 0x80) + main;   // データ1
   *(int *)(this + 0x08) = *(int *)(main + 0x84) + main;   // データ2
   *(int *)(this + 0x0c) = *(int *)(main + 0x88) + main;   // データ3
   *(int *)(this + 0x10) = *(int *)(main + 0x8c) + main;   // データ4
   ```

3. **データ1 と データ4 は全 type が `Doesnt work`**。
   - データ1: `while (type != 0) { 無条件に [1] を出力 }` ＝ switch 自体が存在しない。
   - データ4: 同上（`[4]`）。
   → ffxitool5 はこの 2 本を**まったく解釈していない**。

4. データ2／データ3 は「既知リスト」に載っている type だけが無通知で通る。
   リスト外は `[2]` / `[3]` の `Doesnt work`。

---

## 1. データ2（main+0x84）の既知 type

`FUN_004041b0` 内、`0x4043..` の switch（default が `[2]`）:

```
1, 2, 3, 6, 7, 8, 9, 0x0A, 0x0B, 0x0C,
0x0F, 0x10, 0x11, 0x12, 0x13, 0x16,
0x1E, 0x1F,
0x21 .. 0x2F,
0x41
```

補足（重要）:

- `case 1` だけは特別扱いで、`pbVar2[0x21]`（＝命令の 33 バイト目）が
  `0x0B` / `0x0E` / `0x1D` のとき、`0x1F` / `0x21` の幾何オブジェクトを生成する
  （`FUN_00403c40` / `FUN_00402f60` / `PTR_FUN_004301ec`）。
- **`0x33`〜`0x36` / `0x39` は 0x19 キー表を解決するだけで、既知リストには無い**。
  実際に Cure 10/9 で `kr30` が `type:0x38 / 0x3E` を出すのはこのため
  （別 switch でキー解決だけ行い、既知判定では Unknown になる）。
- `0x14/0x15/0x17〜0x1D` は既知リストに**無い**。

## 2. データ3（main+0x88）の既知 type

同じ関数、default が `[3]` の switch:

```
2, 5, 8,
0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
0x1A, 0x1B, 0x1C, 0x1D,
0x27, 0x28
```

`0x0E` 以下・`0x1E` 以上・`0x29` 以降は**すべて対象外**。

## 3. データ1（main+0x80）／データ4（main+0x8C）

- 既知 type: **なし（0 個）**。終端 `0` 以外は全部 `[1]` / `[4]` の `Doesnt work`。
- アプリ側も `tblA` / `tblD` を実行していないため、方針と実装が一致している
  （`tblD` は main の `+0x8C` に op `0x05` があるかを覗くだけ）。


---

## 4. EffectTest との対応

| ffxitool5 | main オフセット | EffectTest | 実行主体 | 対象 type |
|---|---|---|---|---|
| データ1 | `+0x80` | `tblA` | （未使用） | なし（全 type 対象外） |
| データ2 | `+0x84` | `tblB` | VM#1（生成時に 1 回） | §1 のリスト |
| データ3 | `+0x88` | `tblC` | VM#2（毎フレーム） | §2 のリスト |
| データ4 | `+0x8C` | `tblD` | （未使用） | なし（全 type 対象外） |

→ **構造は一致**。違いは「データ2/3 の既知リストに無い type を EffectTest がどう扱うか」だけ。

## 5. 「`Doesnt work` は対象外」の実装（2026-11-14 追加）

`--nodetypes ffxi|all`（既定 `all` = 従来動作）

- `ffxi`: VM#1（`tblB`）と VM#2（`tblC`）で、**§1/§2 の既知リスト外の type を実行しない**。
  命令長だけ進めて次の命令へ（副作用ゼロ）。データ1/4 は元々実行しない。
- `all` : 全 type を実行（従来どおり。EffectTest は AltanaView 由来の拡張実装を持つため）。

`--opstat` と併用すると、読み飛ばした type が件数付きで出る:

```
[opstat] VM#1 : 対象外(Doesn't work) 7 kinds
[opstat]   skip 0x72 x206
...
```

実装箇所:

- `EffectVm.cpp` … `FfxiTypeB()`（データ2）/ `FfxiTypeC()`（データ3）と
  `g_NodeScope` / `g_NodeSkip`、`EffectSetNodeScope` / `EffectNodeSkipStat`
- `Effect.h` … 同 API の宣言（`EffectNodeScope` ほか）
- `App.cpp` … `--nodetypes` の解析と `[opstat]` 出力

## 6. 実測（`ROM\11\17.DAT` / `--frames 90`）

`--nodetypes ffxi --opstat` で読み飛ばされた type:

| VM | 対象外 type（回数） | 内訳 |
|---|---|---|
| VM#1 | `0x72(206) 0x1D(153) 0x82(37) 0x4C(36) 0x32(33) 0x3D(20) 0x40(20)` | すべて未実装、または `--spin off` で無効 |
| VM#2 | `0x0D(6,141) 0x0E(5,689) 0x5F(1,692) 0x2B(499)` | すべて未実装、または `--spin off` で無効 |

→ **現状の描画は変わらない**（対象外 type は元から no-op）。
`ShotStats` も `all` / `ffxi` で同一:

```
--nodetypes all  : nonBg=9313 maxSum=169
--nodetypes ffxi : nonBg=9313 maxSum=169
```

## 7. 注意 / 残課題

- データ3 の既知リストは `0x0F..0x1D` だが、EffectTest の VM#2 は
  AltanaView 実測から `0x0F..0x21` をキー補間として扱う。
  そのため `--nodetypes ffxi` では `0x1E..0x21` が**対象外に落ちる**
  （`0x1E/0x1F` は ffxitool5 のデータ3 には無い）。
  A/B のときはこの差を意識する。
- データ2 の `0x33〜0x36 / 0x39` は「キー解決はするが既知ではない」ため対象外。
- ffxitool5 の既知リストは「**そのアプリが描ける範囲**」であって
  「FFXI の仕様の全量」ではない。見た目の正解は引き続き AltanaView
  （`docs/ANALYSIS-AltanaView-Draw.md`）。
- データ1/4 が何を表すかは未確定（ffxitool5 も未着手）。EffectTest でも未使用。
