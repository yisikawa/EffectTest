# 実装計画: レビュー指摘（REVIEW-2026-11-10）の解消

- 日付: 2026-11-12
- ブランチ: `feature/schedule`
- 入力: `docs/REVIEW-2026-11-10-Issues-And-Fixes.md`（以下「レビュー」）
- 指示: 「指摘問題点の解消を優先してください」（実装の明示指示とみなす）

---

## 0. 前提（レビューの裏取り結果）

実測で確定した事実（本計画の根拠）:

| 項目 | 実測値 |
|---|---|
| VM#1 のキー op 第 3 dword | `11\17` 8/8・`11\18` 9/9・`12\43` 10/10 が **0x19 チャンク名**（計 27/27） |
| `dest` の連結 | VM#1 `0x27`→dest 6 / `0x28`→dest 3 / `0x2D`→dest 0。VM#2 は同 dest を参照（`0x15`=Scale.x・`0x16`=Scale.y・`0x1B`=Color.a） |
| 0x19 の構造 | `size = 0x18 + 8n`、`+0x18` から `[time(float), value(float)]`。`+0x04` はチャンクヘッダ、`+0x08..+0x17` は **20 チャンクすべて 0**（件数フィールドは無い） |
| 現行の症状 | `--selftest` で `11\18 g003 col=(0.86,0.44,0.24,0.00)`、`11\17 g000 scl=(0.000,0.000,1.000)` |
| 末尾 `(0,0)` | 5/20 に存在。ただし `t01 <= t` で早期 return するため **t01 ≤ 1 では到達せず実害なし**（本計画では特別扱いしない） |

---

## 1. 設計判断（レビュー §13 の未決事項への回答）

| # | 判断 | 理由 |
|---|---|---|
| D-1 | `dword2` は **0x19 チャンク名**。スポーン時（VM#1）に `DatFile::Find(name, 0x19)` で 1 回解決し、`offset+0x18` と件数を保持 | 実測 27/27。オフセット解釈では常に 0 になる |
| D-2 | 件数 = `(chunk.size - 0x18) / 8`（上限 64）。表の終端は `chunk.size` で縛る | ヘッダに件数フィールドが無い（`+0x08..+0x17` は全て 0） |
| D-3 | 未解決（チャンクが無い / サイズ不足）のとき VM#2 は**出力スロットを書かない** | `0` で上書きすると Scale が縮退し α が消える（現行の症状そのもの） |
| D-4 | 補間は現行の走査方式を維持し、**末尾の `(0,0)` は特別扱いしない** | `t01 <= t` で return するため `t01 ≤ 1` では読まれない。件数境界だけ正しくする |
| D-5 | `dest` の境界は `dest + 4 <= SUB_DEST_N` で判定（`Dest()` を返す全 op が対象） | `f[]` は 130 float（添字 0..129）で `Dest(62)` 以降が範囲外。4 float 分の余裕で判定すれば将来の 4 成分書込みにも安全 |
| D-6 | `keyOff` を `keyFirst[64]` + `keyCount[64]`（u32 + u8 = 320 B）へ置換 | レビュー §2.4 の「チャンク参照を保持」。`SubUnit` の増分は +64 B に留める |
| D-7 | 代替処理（RGB=0→白 / α≤0→1）は**既定 ON のまま**、`--colorfallback on\|off` で A/B 可能にする | 撤去は見た目が変わるため、まず計測手段を用意する（レビュー §8.2-1） |
| D-8 | ベース DAT の generator フォールバックは**今回は未解決ログのみ** | `BaseGenerator` と `GeneratorBlock` の統合方針が未決（レビュー §7.2-1）。黙って消える状態だけ解消する |
| D-9 | `× 1.5`（`Gfx.cpp:64`）は変更しない | ゴールデンが GPU 描画に依存し、根拠確認が別途必要なため（レビュー §8.2-2） |

---

## 2. 実装手順

### T-1 キー表の名前解決（問題 1）
- `Effect.h`: `SubUnit::keyOff[64]` → `keyFirst[64]` / `keyCount[64]`。`SUB_FLOAT_N` と `static_assert` を追加。
- `EffectVm.cpp`:
  - `IsKeyOp(op)` を共通化（VM#1 の保存判定と監査で共用）。
  - `EvalKeyTrack(dat, first, count, t01, scale)` に変更（`count` 件・`chunk.size` 内に限定）。
  - `Vm1`: 名前解決して `keyFirst/keyCount` を設定。
  - `Vm2`: `keyFirst[dest]`/`keyCount[dest]` を参照。未解決なら**書かない**。
  - `EffectKeyAudit(dat, gens)`: 全 generator の VM#1 を走査し「キー op 数 / 解決数」を返す（`--selftest` 用）。

### T-2 小さな安全修正（問題 2・3・5）
- `App.cpp`: `bool g_UvScroll` → `int g_UvScroll = 0;`（`cell`=2 が生存）。56〜69 行のインデント修正。
- `Effect.h/.cpp`: `ClearIndex()` を新設し、`BuildInternal` 冒頭で呼ぶ（`m_TexSpecific` を含む）。
- `EffectVm.cpp`: `d` の取得を `dest + 4 <= SUB_DEST_N` に変更（VM#1/VM#2 の両方）。

### T-3 毎フレームの深コピー除去（問題 4）
- `Effect.cpp`: 期限切れが 1 件以上あるときだけ `erase(remove_if(...))`。

### T-4 診断と A/B（問題 6・7）
- `Effect.cpp::Spawn`: generator 未解決時に `[spawn] generator not found: xxxx` をログ。
- `App.cpp`: `--colorfallback on|off`（既定 on）を追加し、ビルボード/geo の代替処理を gate。

### T-5 その他（問題 10 の低リスク分）
- `App.cpp::Log`: `vsprintf_s` → `_vsnprintf_s(..., _TRUNCATE)`。
- `Skeleton.cpp`: nlerp の内積が負なら `k1` を反転。

### T-6 検証の可視化（問題 1・3）
- `--selftest` に以下を追加:
  - `keys      = 27 ops, 27 resolved, 0 unresolved`
  - `texnames  = N texspecific = N`
  - `rebuild   = ok (texnames=N texspecific=N)`（同一 `EffectContext` で 2 回 `Build`）

---

## 3. テスト計画

| # | 対象 | 期待 |
|---|---|---|
| TST-1 | `--selftest 11\17` | `resolved=8`, `unresolved=0` |
| TST-2 | `--selftest 11\18` | `resolved=9`, `unresolved=0` |
| TST-3 | `--selftest 12\43` | `resolved=10`, `unresolved=0` |
| TST-4 | 上記 3 DAT の `frame=59/239` | α が 0 でなくなる（例 `11\18 g003` の α > 0）。Scale が 0 に縮退しない |
| TST-5 | `texnames == texspecific`（3 DAT） | 一致（漏れが無い） |
| TST-6 | `rebuild=ok`（3 DAT） | 2 回目の `Build` で件数が増えない |
| TST-7 | `11\22` | 既存の出力が維持される（変化してよいのはキー解決の影響のみ） |
| TST-8 | ビルド警告 | C4305 が 0 件 |
| TST-9 | `--shot --frames 90`（GPU あり） | `nonBg` / `maxSum` を再計測し `docs/GOLDEN.md` に記録 |

---

## 4. 検証計画

1. Release/x64 をビルド（作業ツリーを汚さないよう `/p:OutDir=%TEMP%\...`）。
2. `--selftest` を `11\17` / `11\18` / `12\43` / `11\22` で実行し、TST-1〜7 を確認。
3. キー解決率 100%（27/27）を確認。未解決があれば名前の取り出し（`p+8` の 4 バイト）を見直す。
4. ゴールデン更新: `--shot` を実行できる環境では `nonBg`/`maxSum` を再計測。GPU 不可なら `--selftest` の数値を基準にする。
5. 既存ドキュメントの旧値は `docs/GOLDEN.md` に一本化し、他は参照に置き換える（レビュー §8.1）。

---

## 5. リスクと対処

| リスク | 対処 |
|---|---|
| 見た目が大きく変わる（α・Scale が復活） | ゴールデンを意図的に更新し、`docs/GOLDEN.md` に理由を記録 |
| dest 61〜63 を正当に使う DAT が存在する | 拒否件数をログに出す（黙って捨てない）。`--opstat` と併用して確認 |
| `--colorfallback off` で黒板が出る | 既定は `on`（現状維持）。A/B 専用 |
| 深コピー除去で寿命処理が変わる | 挙動は等価（`expired` の除去のみ）。`--selftest` の `inst=` で確認 |
