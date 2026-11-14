# Cure `tgt0`（ROM\10\9.DAT）の解析と ffxitool5 準拠の修正 — 2026-11-14

対象: `ROM\10\9.DAT`（ケアル / ID 2801）の Schedule **`tgt0`**
参考: `docs/ANALYSIS-ffxitool5-2026-11-13.md` / `docs/ANALYSIS-ffxitool5-NODETYPES-2026-11-14.md`

---

## 0. 結論

1. Cure 10/9 の Schedule は 3 本: **`main` / `mai0` / `tgt0`**。
   - `main` が `op=0x03 name=tgt0` で **tgt0 を入れ子起動**する。
   - `tgt0` は 7 generator を時間差で発生させ、最後に
     `op=0x03 name=mdam` を実行する。
2. **`mdam` は Cure の DAT に存在しない**。実体は
   **ベース DAT（`ROM\0\0.DAT`）の 0x07 Schedule**（offset 0x81780 / 128 B /
   命令 `01 2B 00`）。従来は「generator が無い」として捨てられていた。
3. 修正: **op 0x03 の 0x07 解決を「ローカル → base DAT」の順**にした。
   これで `tgt0 → mdam` が **frame 200（3.333 s）** で起動する。
4. ffxitool5 の `Doesnt work` は **generator 内 VM データ1〜4 の type**の話であり、
   tgt0 の「どの generator をいつ出すか」とは独立。tgt0 の構成は ffxitool5 と一致する。

---

## 1. 0x07 Schedule の一覧と関係

`--action <dat>` の実測:

| 名前 | size | 命令数 | 長さ | 役割 |
|---|---|---|---|---|
| `main` | 240 B | 9 | 200 f (3.333 s) | `shwh`/`0303` を設定し、**`tgt0` を起動** |
| `mai0` | 304 B | 13 | 244 f (4.067 s) | tgt0 と同型（`kr30` 81 f / `mdam` 41 f と 1〜2 f 違い） |
| `tgt0` | 256 B | 11 | 239 f (3.983 s) | **対象**（7 generator + `mdam`） |

関係:

```
main  --op 0x03-->  tgt0
mai0  --op 0x03-->  mdam          （mai0 は tgt0 の複製に近い）
tgt0  --op 0x03-->  mdam  ── base DAT(ROM\0\0.DAT) の 0x07
```

## 2. `tgt0` の命令列（実測）

```
+0x050 op=0x01 len=2 delay=0  param=0
+0x058 op=0x20 len=3 delay=0  param=30
+0x064 op=0x02 len=4 delay=10 param=30  name=kr11
+0x074 op=0x02 len=4 delay=5  param=85  name=prn1
+0x084 op=0x02 len=4 delay=11 param=85  name=prn0
+0x094 op=0x02 len=4 delay=23 param=60  name=kr12
+0x0A4 op=0x02 len=4 delay=70 param=159 name=kr10
+0x0B4 op=0x02 len=4 delay=1  param=120 name=gr00
+0x0C4 op=0x02 len=4 delay=80 param=86  name=kr30
+0x0D4 op=0x03 len=4 delay=39 param=0   name=mdam
+0x0E4 op=0x00 len=2
```

delay は「その命令の実行後」に加算される（`EffectContext::Run` と同じ）。
したがって発生フレームは次のとおり（60 fps）:

| generator | 発生フレーム | 時刻 |
|---|---|---|
| `kr11` | 0 | 0.000 s |
| `prn1` | 10 | 0.167 s |
| `prn0` | 15 | 0.250 s |
| `kr12` | 26 | 0.433 s |
| `kr10` | 49 | 0.817 s |
| `gr00` | 119 | 1.983 s |
| `kr30` | 120 | 2.000 s |
| **`mdam`** | **200** | **3.333 s** |
| 終端 | 239 | 3.983 s |

> `--trace` は 20 フレーム間隔のサンプルなので、上の表は命令列から算出した値。
> 修正後の実機ログ `[sub] mdam: base insts=3 at frame=200` と一致する。

## 3. `mdam` の実体（ベース DAT）

```
ROM\0\0.DAT  offset=0x81780  size=128  type=0x07  name="mdam"
  +0x050 op=0x01 len=2 delay=0
  +0x058 op=0x2B len=3 delay=0
  +0x064 op=0x00 len=2
```

- `0x01` は prologue（当方は no-op）、`0x2B` は**未解明**（§6）。
- **粒子を出さない**ため、正しく起動しても描画は変わらない（＝従来の「欠落」は
  「見えない sub が起動していない」状態だった）。
- 同じ `mdam` は `ROM\11\17.DAT`（Fire）の main からも参照されており、
  従来は同じ理由で落ちていた（本修正で解決。画像は不変）。

---

## 4. ffxitool5 との突き合わせ（0x07 ヘッダ）

`FUN_00403a70`（0x403a70）は 0x07 チャンクを受け取り、次を設定する:

```c
*(this+0x04) = *(int*)(chunk+0x20) + chunk;   // stream1
*(this+0x08) = *(int*)(chunk+0x24) + chunk;   // stream2
*(this+0x0c) = *(int*)(chunk+0x28) + chunk;   // stream3
*(this+0x14) = *(int*)(chunk+0x2c);           // 長さ
```

Cure 10/9 の実データはこの解釈と一致する:

| 0x07 | +0x20 | +0x24 | +0x28 | +0x2C |
|---|---|---|---|---|
| `main` | 0x40 | **0x50** | 0xE0 (=size-16) | **200** |
| `mai0` | 0x40 | **0x50** | 0x120 (=size-16) | **244** |
| `tgt0` | 0x40 | **0x50** | 0xF0 (=size-16) | **239** |

→ `+0x24` が指す **0x50 が命令列**で、当アプリの `ParseTimeline`（+0x50 起点・
`len = byte[1] & 0x1F`）と同じ。`,`+0x2C` は**フレーム数**で、当方が命令の delay 合計から
求めた duration（200 / 244 / 239）と一致する。

**したがって「tgt0 を ffxitool5 と同じ構成で動かす」＝ 当アプリの +0x50 解釈で正しい。**

### 4.1 `Doesnt work` との関係（誤解しやすい点）

ffxitool5 の `[n] typeXX Unknown. Doesnt work` は **generator（0x05）内の
データ1〜4 の type**に対する警告で、**Schedule の op（0x02/0x03）とは別系統**。

- tgt0 が `kr11`/`prn1`/`prn0`/`kr12`/`kr10`/`gr00`/`kr30` を出すこと自体は
  ffxitool5 でも同じ（`Doesnt work` でも**インスタンスは作られる**）。
- 違いが出るのは各 generator の**内部 VM の type**の解釈だけ。
  その範囲は `--nodetypes ffxi` で ffxitool5 準拠に揃えられる
  （`docs/ANALYSIS-ffxitool5-NODETYPES-2026-11-14.md`）。

## 5. 修正内容（2026-11-14）

| 変更 | 内容 |
|---|---|
| `EffectContext::StartSubTimelineFrom` | 0x07 を **呼び出し元 DAT → base DAT** の順で解決。base から起動した sub は命令も生成も base の DAT で行う |
| `SubTimeline::dat` | sub ごとの生成元 DAT（base の 0x07 を正しく実行するため） |
| `EffectContext::FindBaseGenerator` | base の 0x05 を `GeneratorBlock` に変換（テクスチャフレーム/0x1F は未解析）。`unique_ptr` で安定保持 |
| `EffectBase::BaseGenerator` | `+0xAC` の 4 文字リンク名を追加（base の 0x20 借用に必要） |
| `ExecSub` | op 0x03（入れ子）を処理。op 0x02 は sub の DAT で生成 |
| `--sched <name>` | `--shot` で main 以外の 0x07 を再生（tgt0 の検証用） |
| ログ | `[sub] <name>: base|local insts=N at frame=F ops=..`、`[build] sched=..`、`[shot] .. subs=.. base=..` |

## 6. 検証

```powershell
.\x64\Release\EffectTest.exe --shot "$ROM\10\9.DAT" tgt0.png --frames 260 --sched tgt0
# -> [build] sched=tgt0
# -> [sub] mdam: base insts=3 at frame=200 ops=01 2B 00
# -> [shot] ... subs=0 base=1 ok=1     （260f は 1 周して Reset 済み）
tools\ShotStats.ps1 -Path tgt0.png
# -> nonBg=2488 maxSum=322
```

- `mdam` の「generator not found」は解消（11\17 の mdam も同様）。
- 既定ゴールデンは不変: 11\17=9,313/169、11\18=3,583/449、11\22=962/303、12\43=4,764/263。
- `--selftest`（11\17）: keys 8/8/0、texnames==texspecific、rebuild=ok。
- `12\43` の `[sub] hit1: local ... at frame=0` も従来どおり。

## 7. 未確定（意図的に残す）

1. **タイムライン op `0x20` / `0x2B`** の意味。
   - tgt0 の `0x20`（param=30）と mai0 の `0x20`（param=202）で値が違い、
     `mdam` の `0x2B` は base 内で唯一の非 prologue 命令。
   - ffxitool5 側のタイムライン walker は未特定（`and reg,0x1F` の byte 探索では
     CRT しか当たらない）。**当方は現状 no-op**（描画に寄与しないことは実測済み）。
2. base の 0x05 は `linkName` 以外（`0x21` フレーム / `0x1F` ジオメトリ）を
   パースしていない。base の sub が「白い板」になる可能性がある。
3. `main` の `0x56` / `0x53` / `0x0B` / `0x31` / `0x32` などは従来どおり未実装。

---

## 8. 3D 座標の修正（命令長の引数オーバーリード）— 2026-11-14

### 8.1 症状

`tgt0` の `kr10` が **z = -41 m** まで飛ぶ（1 フレーム 0.5 m）。
`kr11` も y = -5 m まで落ちる。エフェクトが原点周辺（数 m）に収まらない。

### 8.2 原因（実データで確定）

`kr10` の VM#1（データ2）は次の 2 命令を持つ:

```
op=0x08 len=2  arg0=-0.005          ← 引数は 1 個だけ（命令は 8 バイト）
op=0x72 len=3  arg0=-0.5            ← 次の命令
```

当方は `arg` を `p+4` の配列として **len を無視して arg[0..2]** を読んでいた。
そのため `0x08` の `arg[2]` が **次命令 `0x72` の arg0 = -0.5** を拾い、
`Dest(0)` に -0.5 が入る。次に VM#2（データ3）の `0x02`（**len=1・引数なし**）が
毎フレーム `Pos += Dest` するため、**Pos.z が -0.5/フレームで無限に流れた**。

→ 3D 座標が壊れていたのは VM のロジックではなく、**命令長の扱い**だった。

### 8.3 修正

- `EffectVm.cpp`（VM#1 / VM#2）: 引数を **len-1 個**に制限して読む
  （`argBuf[12]` に詰め替え。範囲外は 0）。
- キー表の名前（3 dword 目）は **len>=3 のときだけ**読む。
- 命令長 `len` はループ先頭で 1 回だけ計算（末尾の再計算を削除）。

### 8.4 結果（実測）

| 項目 | 修正前 | 修正後 |
|---|---|---|
| `kr10` の z（f=120） | -36.15 | **-0.148（一定）** |
| `kr10` の x（f=60→120） | — | 0.62→0.32（-0.005/frame = `0x08` の arg0 どおり） |
| 3D 広がり max\|pos\|（f=90, tgt0） | z 数十 m | **x 0.96 / y 5.15 / z 0.73** |

`y` が 5 m まで伸びるのは `kr11` の VM#2 `0x02` が
VM#1 `0x02` の dest（`(0,-0.04,0)`）を毎フレーム積むため（= 2.4 m/s）。
これは**データどおり**（`0x02` は両ストリームで「dest を Pos に積分」）。

既定ゴールデンの更新（`docs/GOLDEN.md` §2）:

| DAT | 修正前 | 修正後 |
|---|---|---|
| `11\17` | 9,313 / 169 | **10,343 / 204** |
| `11\18` | 3,583 / 449 | **3,948 / 454** |
| `11\22` | 962 / 303 | **970 / 303** |
| `12\43` | 4,764 / 263 | **4,661 / 263** |

`--selftest`（11\17）は不変（keys 8/8/0、texnames 一致、rebuild=ok）。
Cure `tgt0`（`--frames 260 --sched tgt0`）も 2,488 / 322 で不変。
