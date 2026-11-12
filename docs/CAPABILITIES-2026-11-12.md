# コード上でできるようになったこと（2026-11-12）

対象: ブランチ `feature/schedule` / `8159399`（レビュー文書）以降の 4 コミット
（`60bed3f` レビュー指摘の修正 → `768a5cd` ゴールデン一本化 → `34e4820` 円周配置 → `b2cac23` 文書）

> この文書は「**何ができるようになったか**」だけを書いた一覧です。
> なぜ直したか（指摘の根拠）は `docs/REVIEW-2026-11-10-Issues-And-Fixes.md`、
> 正しい数値は `docs/GOLDEN.md`、検討の経緯は `docs/superpowers/plans/2026-11-12-*.md` を参照。

---

## 1. 一覧（できるようになったこと）

| # | できること | 変更前 | 変更後 | 使い方 | 確認方法 |
|---|---|---|---|---|---|
| 1 | **キーフレーム補間が実際に効く** | VM#1 のキー op の第 3 dword をファイルオフセットと誤解し、常に 0 を書いていた（α=0・Scale=0） | `0x19` チャンク名として解決し、`+0x18` の `[time,value]` を補間 | 既定（フラグ不要） | `--selftest` の `keys ... unresolved=0` と、インスタンス行の `col=` / `scl=` |
| 2 | **粒子を円周上に配置できる** | `0x06` は球面乱数（半径ばらつき）、`0x1F` は no-op → 粒子が 1 点に重なり直線状 | `0x06`/`0x1F` の引数を楕円半径として円周上に配置（位相のみ乱数） | 既定 `--ring on`。`--ring off` で旧挙動 | `--trace` の半径 / `tools\RingStats.ps1` の `hollow` |
| 3 | **円周半径を切り替えて比較できる** | なし | 半径の出どころ（`arg0/arg1` か `arg7` の絶対値）と倍率を選択 | `--ringradius xy\|dist`、`--ringscale N` | 同上 |
| 4 | **色/α の代替処理を切り離して評価できる** | 常に代替（RGB=0→白 / α≤0→1）で不具合が隠れていた | `--colorfallback off` で VM の生値をそのまま描画 | `--shot --colorfallback off` | `11\18`: on 3,488 / off 3,473 |
| 5 | **`--uvscroll cell` が動く** | `bool` に 2 を代入して `on` に化け、`cell` 分岐が到達不能（警告 C4305） | `int` 化して 3 モードが区別される | `--shot --uvscroll off\|on\|cell` | `off` と `on` で PNG ハッシュが変化 |
| 6 | **再構築してもテクスチャ索引がずれない** | `m_TexSpecific` のクリア漏れ。List で DAT を切り替えると誤テクスチャ/白 | `ClearIndex()` に一本化 | 既定（GUI の連続選択で効く） | `--selftest` の `texnames == texspecific` と `rebuild = ok` |
| 7 | **未知 DAT でも範囲外書込みしない** | `dest` の検査が `dest < 64` のみで、`dest=62/63` は配列外 | `dest + 4 <= SUB_DEST_N` で判定＋`static_assert` 2 本 | 既定 | ビルドが通る（assert 成立）。破損 DAT でもクラッシュしない |
| 8 | **generator 未解決がログで分かる** | `Spawn` が黙って `return`（表示が消えるだけ） | `[spawn] generator not found: …` をログ＋`--selftest` の `missing=` | 既定 | `effecttest_log.txt` / `--selftest` |
| 9 | **寿命切れの除去で深コピーしない** | 毎フレーム全インスタンスをコピーして `swap` | 期限切れがあるときだけ `erase(remove_if)` | 既定（挙動は同じ） | `--selftest` の `inst=` が不変 |
| 10 | **長いログで落ちない** | `vsprintf_s` が 1024 B を超えると無効パラメータで異常終了 | `_vsnprintf_s(..., _TRUNCATE)` で切り詰め | 既定 | 長いパス名の DAT でも終了しない |
| 11 | **骨の回転が遠回りしない** | nlerp で内積が負でも反転せず 180° 超で逆回転 | 内積が負なら片方を反転 | 既定（`--model`/`--motion` 使用時） | `--skel` / 骨付き `--shot` の見た目 |
| 12 | **粒子配置を数値で判定できる** | 目視のみ | `tools\RingStats.ps1`（bbox/aspect/fill/hollow/peak） | `tools\RingStats.ps1 -Path out.png` | 出力 1 行 |

---

## 2. 詳細

### 2.1 キーフレーム補間（`0x19` 表）が効くようになった

**できること**: VM#1 が設定するキー表を VM#2 が毎フレーム参照し、
Pos / Move / Scale / Color / UV の**成分ごと**に線形補間した値が入る。

```
VM#1（生成時に 1 回）                       VM#2（毎フレーム）
op 0x21..0x2F / 0x33..0x36 / 0x59..0x5B      op 0x0F..0x21
  dword2 = "k006"（0x19 チャンク名）   →       slot[(op+0x11)*4] = EvalKeyTrack(...)
  ↓ Find(name, 0x19)                          ↑
  keyFirst[dest] = chunk.offset + 0x18        keyCount[dest] 件だけ読む
  keyCount[dest] = (chunk.size - 0x18) / 8
```

| 項目 | 内容 |
|---|---|
| データ構造 | `SubUnit::keyFirst[64]`（u32） / `keyCount[64]`（u8）。`keyCount == 0` = 未解決 |
| 補間関数 | `EvalKeyTrack(const DatFile&, uint32_t first, uint32_t count, float t01, float scale)` |
| 未解決時 | **出力スロットを書かない**（0 で上書きしないので、`Clear()` の初期値 1 が残る） |
| 監査 | `KeyAudit EffectKeyAudit(const DatFile&, const std::vector<GeneratorBlock>&)` |

**効果（実測、`--selftest` の `frame=59`）**

| DAT / generator | 変更前 | 変更後 |
|---|---|---|
| `11\18` `g003` | `col=(0.86,0.44,0.24,0.00)` | `col=(0.86,0.44,0.24,0.58)` |
| `11\17` `g000` | `scl=(0.000,0.000,1.000)` | `scl=(0.800,0.705,1.000)` |
| `11\17` `g005` | `scl=0 / α=0.00` | `scl=(1.660,1.660,1.000) / α=0.49` |
| `12\43` 全 generator | `α=0.00` | `α=0.50` |

---

### 2.2 粒子を対象の周り（円周・らせん）に配置できる

**できること**: VM#1 の `0x06`（旧: 球面乱数）と `0x1F`（旧: 未実装）が、
引数の先頭 2 個を**楕円半径**として、粒子を**円周上に均等に**配置する。
位相は乱数ではなく**粒子番号の黄金角**（`frac(i·1/φ)`）で割り付けるので、
何個出ても円周を均等に覆う（乱数だとムラが出て 1 本の線に見える）。
`--arrange spiral` では同じ位相に軸方向のずらしを加えて**らせん**にする。

| 設定 | できること |
|---|---|
| `--arrange ring`（既定） | 円周に均等配置 |
| `--arrange spiral` + `--spiralturns N` | らせん（巻き数 N） |
| `--arrange line` | 旧挙動（`0x06`=球面乱数 / `0x1F`=no-op） |
| `--ringplane xy\|xz\|yz` | 円周を張る面（既定 xy＝進行方向に垂直） |
| `--ringscale N`（既定 4） | 半径の倍率。引数の半径 0.1〜0.5 m はサブ粒子スケールで、ビルボード 2〜4 m より小さいため、そのままでは円周が埋もれる |
| `--ringradius xy\|dist` | `0x1F` の半径の出どころ（`dist` = `arg7` の絶対値 0.4〜0.8） |

| 引数 | 実データ例 | 意味（本実装） |
|---|---|---|
| `0x06` `(arg0, arg1)` | `(0.2,0.2)` `(0.2,0.6)` `(0,0.5)` `(0.6,0)` | 楕円半径 `(rX, rY)` |
| `0x1F` `(arg0, arg1, …)` | `(0.2,0.2,1,1,1,0,0,-0.8,1.5,0,0)` | 同上。`arg7`（-0.4..-0.8）/`arg8`（0.8..1.5）は未確定 |

**効果（実測）**

配置（`--trace` の生成直後 `elapsed<0.06 s`、倍率 1 で測定）:

| DAT / gen | op（半径） | `--arrange line` | `--arrange ring` |
|---|---|---|---|
| `11\17` `g002` | `0x1F` (0.2, 0.2) | r=0.0084 / CV 0.707（1 点に重なる） | **r=0.190 / CV 0.158** |
| `11\17` `g003` | `0x06` (0.2, 0.2) | r=0.236 / CV 0.350（球面） | **r=0.196 / CV 0.130** |

画像（`--frames 90`、既定 = ring / xy / scale 4）:

| DAT | `--arrange line` | 既定（ring） | spiral（turns 2） |
|---|---|---|---|
| `11\17` | 5,023 / 129, hollow **2.851**（中詰まり＝線状） | 9,310 / 166, hollow **0.774**（リング状） | 10,523 / 144, hollow 0.774 |
| `11\18` | 3,053 / 465, hollow 7.520 | 6,257 / 353, hollow **1.821** | 6,699 / 320, hollow 2.104 |
| `12\43` | 3,615 / 244, hollow 1.333 | 4,780 / 263, hollow **1.239** | 5,320 / 329, hollow 1.277 |

比較画像: `docs/shots/2026-11-12-{line,ring,spiral}-11_17.png`。

---

### 2.3 状態遷移が安全になった

| 変更 | できること |
|---|---|
| `EffectContext::ClearIndex()` | 再構築時に `m_Timeline` / `m_Generators` / `m_Textures` / `m_TexNames` / **`m_TexSpecific`** / `m_Instances` / `m_Subs` を**1 か所で**クリア。クリア漏れの再発を防ぐ |
| `dest + 4 <= SUB_DEST_N` | `Dest(dest)` を返す全 op が配列外を書かない。破損 DAT・未知 DAT でも未定義動作にならない |
| `static_assert` 2 本 | `SubObject::f` の要素数（`SUB_FLOAT_N`）と `SUB_DEST_N` の不整合をコンパイル時に検出 |
| `erase(remove_if)` | 期限切れ 0 件のフレームでコピーが発生しない（`SubUnit` は 800 B 超 × 粒子数） |
| `_vsnprintf_s(_TRUNCATE)` | ログが長くても異常終了しない |

---

### 2.4 検証の観測点が増えた（`--selftest`）

`--selftest` の出力に次の 4 行が加わった。**すべて機械判定できる**。

```
keys      = 8 ops, 8 resolved, 0 unresolved      ← VM#1 キー表の解決率（unresolved=0 が正常）
texnames  = 5 texspecific = 5                    ← 0x20 の数と specific 名の数（一致が正常）
missing   = 3 generator(s) unresolved            ← タイムラインが参照した generator の未解決数
rebuild   = ok (texnames=5->5 texspecific=5->5 gen=8->8)   ← 2 回 Build しても累積しない
```

| DAT | keys（ops/resolved/unresolved） | texnames=texspecific | rebuild | missing |
|---|---|---|---|---|
| `11\17` | 8 / 8 / 0 | 5 = 5 | ok | 3 |
| `11\18` | 9 / 9 / 0 | 7 = 7 | ok | 2 |
| `11\22` | 33 / 33 / 0 | 6 = 6 | ok | 2 |
| `12\43` | 10 / 10 / 0 | 6 = 6 | ok | 0 |

---

## 3. 追加された API（`Effect.h`）

コードから直接使える関数・型。すべて **既定は現状維持**（呼ばなければ従来動作）。

| 宣言 | できること |
|---|---|
| `void EffectSetRing(bool on)` / `bool EffectRing()` | `0x06`/`0x1F` の円周上配置を切り替える |
| `void EffectSetRingScale(float s)` | 円周半径の倍率（`<=0` は 1.0 に丸め） |
| `void EffectSetRingDist(bool on)` / `bool EffectRingDist()` | `0x1F` の半径に `arg7` の絶対値を使う（既定は `arg0/arg1`） |
| `struct KeyAudit { int ops, resolved, unresolved; }`<br>`KeyAudit EffectKeyAudit(const DatFile&, const std::vector<GeneratorBlock>&)` | VM#1 を**実行せずに**走査し、キー表の解決率を返す（テスト用） |
| `void EffectLogLine(const char* fmt, ...)` | `Effect.cpp` から `effecttest_log.txt` へ出力する（`Log()` は App.cpp の static のため） |
| `size_t EffectContext::TexNameCount()` / `TexSpecificCount()` | 0x20 の数と specific 名の数を検査する |
| `int EffectContext::MissingGenerators()` | `Spawn` で generator 名を解決できなかった回数 |

内部型の変更（外部から見た効果）:

| 旧 | 新 | 効果 |
|---|---|---|
| `SubUnit::keyOff[64]`（u32 オフセット） | `keyFirst[64]` + `keyCount[64]` | 「表の先頭と件数」を保持。チャンク外を読まない。約 +64 B/粒子 |
| `float f[(SUB_DEST + SUB_DEST_N*4)/4]` | `float f[SUB_FLOAT_N]`（同値 = 130） | `static_assert` で不整合を検出可能に |

---

## 4. 追加された CLI（`--shot` 系）

| フラグ | 既定 | できること |
|---|---|---|
| `--ring on\|off` | **on** | `0x06`/`0x1F` の円周上配置（`off` = 旧: 球面乱数 / no-op） |
| `--ringradius xy\|dist` | xy | `0x1F` の半径の出どころ（`dist` = `arg7` の絶対値） |
| `--ringscale N` | 1 | 円周半径の倍率（`N>0`。データ値のままではビルボードに埋もれるため確認用） |
| `--colorfallback on\|off` | **on** | RGB=0→白 / α≤0→1 の代替の有無（`off` で VM の生値） |
| `--uvscroll off\|on\|cell` | off | **`cell` が実際に選べるようになった**（旧: `bool` のため `on` に化けた） |

ログにも 1 行出る: `[shot] ringscale=4.000 ring=1`。

---

## 5. 追加された検証ツール

| ツール | できること |
|---|---|
| `tools\RingStats.ps1 -Path <png> [-Step 2]` | 画像が「中詰まり（直線・塊）」か「中抜け（円周・リング）」かを 1 行で判定 |

```
path=out.png bbox=289x301 aspect=0.96 fill=0.973 hollow=2.145 peak=0.304 rows=91
```

| 指標 | 意味 |
|---|---|
| `bbox=WxH` | 非背景画素の外接矩形 |
| `aspect` | 幅/高さ（参照の「柱」は 0.40 前後） |
| `fill` | 非背景画素 / bbox 面積（円周配置だと小さくなる） |
| `hollow` | 中央帯の平均輝度 ÷ 外周帯の平均輝度。**< 1.0 = 中抜け（リング状）** |
| `peak` | 中央行の輝度ピーク位置（0=中心 / 1=端） |

---

## 6. まだできないこと（既知の制約）

| 項目 | 現状 | 影響 |
|---|---|---|
| `0x1F` の引数 `arg7`（-0.4..-0.8）/ `arg8`（0.8..1.5） | 意味未確定。`arg7` を半径にすると `11\18` の `hollow` が 6.58 → 3.94 に改善するため半径の可能性が高い | 既定は `arg0/arg1`。リングが画像で弱い一因 |
| 円周が画像で見えにくい | データの半径 0.1〜0.7 m に対しビルボードが 2〜4 m | `--ringscale 4` で `hollow 0.954`（リング状）になるが縦横比 1.55 |
| リングの面 | XY 固定（進行方向に垂直） | `0x21` クワッドの向きとの関係は未確認 |
| ベース DAT の generator フォールバック | 未解決を**ログに出すだけ**（`missing=`）。`EffectBase::FindGenerator/FindTimeline` は未使用のまま | 共有 generator を使うエフェクトは表示されない（原因は分かる） |
| `× 1.5` のゲイン（`Gfx.cpp:64`） | 根拠不明のまま | 明るさが参照とずれる可能性 |
| `0x19` の外挿規則・末尾 `(0,0)` | 「先頭キーより前は最初の区間の傾きで外挿」「末尾 `(0,0)` は `t01≤1` では読まれない」で運用 | 特殊な表で値がずれる可能性 |
| VM#1 `0x72` / `0x1D` / `0x2D` / `0x4C` / `0x82` / `0x32`、VM#2 `0x5F` / `0x29` | 未実装（IP 前進のみ） | 位置・形状の再現度に影響 |
| CLI の先頭一致 | `strncmp(lpCmdLine, "--skel", 6)` のため `--skeleton` も `--skel` と解釈 | 誤入力時に意図しない動作 |

---

## 7. 確認手順（コピペで実行）

```powershell
$ROM = 'C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM'
$exe = '.\x64\Release\EffectTest.exe'

# 1) キー表が解決していること（unresolved=0）
& $exe --selftest "$ROM\11\18.DAT"; Start-Sleep -Milliseconds 300
Select-String effecttest_selftest.txt -Pattern '^keys|^texnames|^rebuild'
#  -> keys = 9 ops, 9 resolved, 0 unresolved / texnames = 7 texspecific = 7 / rebuild = ok

# 2) 円周配置の A/B（半径とばらつき）
foreach ($ring in 'off','on') {
  & $exe --shot "$ROM\11\17.DAT" "ring_$ring.png" --frames 90 --ring $ring --trace
  Copy-Item effecttest_trace.csv "trace_$ring.csv"
}
#  -> trace_*.csv を name でまとめ、elapsed<0.06 の粒子の XY 半径 / CV を比較
#     ring=on で r≈0.19 / CV≈0.16（off は r≈0.008 / CV≈0.71）

# 3) 画像がリングかどうか
& $exe --shot "$ROM\11\17.DAT" out.png --frames 90
tools\RingStats.ps1 -Path out.png
#  -> hollow < 1.0 ならリング状。データ値のままでは 2.145（--ringscale 4 で 0.954）

# 4) 代替処理の A/B
& $exe --shot "$ROM\11\18.DAT" cf_off.png --frames 90 --colorfallback off
#  -> on 3,488 / off 3,473（ShotStats.ps1）
```

---

## 8. 関連文書

| 文書 | 内容 |
|---|---|
| `docs/GOLDEN.md` | 数値の正（`--selftest` / `--shot` / 形状指標 / フラグ別 A/B / 広域回帰 40/40） |
| `docs/REVIEW-2026-11-10-Issues-And-Fixes.md` | 指摘の根拠と §15 実装状況 |
| `docs/superpowers/plans/2026-11-12-fix-review-issues.md` | レビュー対応の設計判断（D-1〜D-9）と手順 |
| `docs/superpowers/plans/2026-11-12-ring-arrangement.md` | 円周配置の仮説・実装・検証・残課題 |
| `docs/ANALYSIS-AltanaView-Draw.md` §10.6c/§10.6d | `0x19` キー表と円周配置の解析メモ |
| `ReadMe.md` | フラグ一覧・`--selftest` の観測点・忠実度 |
| `.clinerules/00-workflow-memory.md` | 現行ゴールデンの要約とフラグ表 |
