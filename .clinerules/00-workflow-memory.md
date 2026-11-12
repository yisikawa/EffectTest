# 開発ワークフロー（記憶事項）

このリポジトリ（EffectTest）の開発ルール。全体ルールは
`D:\tools\AltanaView-master\.clinerules\00-workflow-memory.md` と同一。

## git
- **git 前提**。`main` の初期コミットは `0e05f8a`（2026-11-08 / 161 ファイル）。`core.autocrlf=false`。
- 設計段階の変更（`EffectBase` ほか 9 箇所）は**この初期コミットに含まれる**（遡って分離できない）。
- 現在の作業ブランチ: **`feature/schedule`**（`design/list-to-screen` から分岐）
- 実装は `feature/*` で行い、小さなコミット（日本語メッセージ）で積む。作業後は push まで行う。
- 旧ブランチ: `design/list-to-screen` / `main`

## 進め方（順序厳守）
1. 設計書（コード変更禁止）
2. 実装計画（`docs\superpowers\plans\YYYY-MM-DD-<feature>.md`）
3. テスト計画
4. 検証計画
5. **ユーザーの明示指示後に実装**

## 検証
- `--list` / `--base` / `--action` / `--selftest` のヘッドレス出力をゴールデン値と比較
- 描画は `--shot` の PNG を `tools/ShotStats.ps1` で画素検査（`nonBg` / `maxSum`、step=4・57,600 サンプル）
- ビルド: MSBuild v143 / Release / x64
  ```
  & 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
    EffectTest.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
  ```
- ROM: `C:\Program Files (x86)\PlayOnline\SquareEnix\FINAL FANTASY XI\ROM`

## 現行ゴールデン（2026-11-09 時点・要更新時は本項を直す）
```
--frames 90 / 既定（--layout off・--emit on・--geo off）
  11\17 = 9,310 / 166    11\18 = 6,257 / 353
  11\22 =   962 / 298    12\43 = 4,780 / 263
旧挙動の再現: --layout on --emit off で 3,625 / 3,098 / 5,686 / 3,628
  直線配置（旧）: --arrange line で 5,023 / 129・3,053 / 465・3,615 / 244
広域回帰: 11\0-11\19 / 12\0-12\19 = 40/40
--selftest: keys unresolved=0（11\17=8/8, 11\18=9/9, 11\22=33/33, 12\43=10/10）
            texnames==texspecific / rebuild=ok
詳細と正は docs/GOLDEN.md（2026-11-12 更新・ここは要約）
```

## 主要フラグ（2026-11-09 追加分）
| フラグ | 既定 | 意味 |
|---|---|---|
| `--emit on\|off` | on | 連続放出モデル（AltanaView の粒子プール） |
| `--emitrate N` | 24 | 放出レート（個/秒） |
| `--layout on\|off` | **off** | 位置 0 のインスタンスを横並び（デバッグ。on にすると横帯になる） |
| `--zup on\|off` | off | 粒子位置の Z-up→Y-up 変換（実験） |
| `--geo on\|off` | off | `0x1F` の 3D 形状を描画 |
| `--opstat` | off | 実行 VM op を `effecttest_log.txt` に集計出力 |
| `--colorfallback on\|off` | **on** | RGB=0→白 / α≤0→1 の代替（off で A/B。2026-11-12 追加） |
| `--arrange ring\|spiral\|line` | ring | `0x06`/`0x1F` の初期配置（均等円周 / らせん / 旧。2026-11-12） |
| `--ringplane xy\|xz\|yz` | xy | 円周を張る面（2026-11-12） |
| `--ringscale N` | **4** | 円周半径の倍率（引数の 0.1..0.5 m はビルボードより小さい） |
| `--spiralturns N` | 1 | らせんの巻き数 |
| `--ringradius xy\|dist` | xy | `0x1F` の半径（`dist` = `arg7` の絶対値） |
| `--dist N` / `--yaw R` / `--pitch R` | 14 / π / 0.35 | カメラ。参照の AltanaView.ini は **dist 2.785**（2026-11-12 追加） |
| `--res WxH` | 1280x720 | 撮影解像度。参照の 3D ビューは **625x512**（2026-11-12 追加） |
| `--gain N` | 1.5 | 色ゲイン（PS の ×1.5 を相殺。A/B 用） |
| `--keymode incr\|abs` | incr | キー補間（incr = 実測どおり／abs = 旧） |

## 解析メモ（要点のみ・詳細は docs）
- 逆コンパイル: `%TEMP%\AltanaViewRE_out_unpacked\04_decompiled.c`
  展開済み exe: `%TEMP%\av_unpack\AltanaView_unpacked.exe`（dist の exe は UPX 圧縮）
- **AltanaView の起動条件**: `List` は**カレントディレクトリ**基準で解決される。
  `D:\tool\AltanaView-master` 以外から起動すると `List folder "…\List" does not exist.` で停止する。
  → 解析時は `Start-Process -WorkingDirectory 'D:\tool\AltanaView-master'` で起動し、
  その後 Frida で **attach**（spawn は CWD を継承して失敗する）
- **D3DX は静的リンク**（`d3dx9_*.dll` は読み込まれない）。`0x1F` の回転は
  実行時 IAT `0x457084`=RotZ(`0x42ed2b`)、`0x457080`=RotY(`0x42ec88`)、
  `0x45700c`=合成(`0x436695`)。**Z(arg5) → Y(arg6) の順**
- **VM の役割**: VM#1（`FUN_0040e650`）= **粒子生成時に 1 回**、VM#2（`FUN_0040f4b0`）= 毎フレーム
- 寿命は `ctx+0x1c8`（`(gen+0x20 >> 16)/60` 秒）。`LifeTime`（op01 +0x22）は未使用
- 定数（`.rdata`）: `0x452C38`=1/65536、`0x452C50`=60、`0x452C54`=π、`0x452C58`=2π、
  `0x452C60`=π/2、`0x452C68`=π/65536、`0x452C70`=2π/65536
- `0x1F`: ヘッダ 0x30 固定 / 頂点 36 B（`+0x00..04` half xyz、`+0x1C` half u、`+0x20` half v）
- **0x19 キー表**: `size = 0x18 + 8n`、`+0x18` から `[time(float), value(float)]`。
  `+0x04` はチャンクヘッダ、`+0x08..+0x17` は 0（件数フィールドは無い → `(size-0x18)/8`）。
  VM#1 のキー op の第 3 dword は**ファイルオフセットではなく 0x19 チャンク名**（`"k006"` 等）
- **円周配置**: VM#1 `0x06`（len=4, 引数 2 個）/ `0x1F`（len=12, 引数 11 個）は
  「円運動＋乱数位相」。先頭 2 引数を XY の楕円半径として `pos += (cosφ·rX, sinφ·rY)`。
  実測: `11\17` `g002` の `0x1F`(0.2,0.2) → r=0.190 / CV 0.158（旧 0.008 / 0.707）
- 進捗の詳細: `docs/PROGRESS-2026-11-09.md`、解析: `docs/ANALYSIS-AltanaView-Draw.md`、
  ゴールデン: `docs/GOLDEN.md`、レビュー対応: `docs/REVIEW-2026-11-10-Issues-And-Fixes.md`、
  **できること一覧: `docs/CAPABILITIES-2026-11-12.md`**、
  **周知事項（逆コンパイルで確定した前提）: `docs/DESIGN-Particle-Handle-2026-11-12.md` §9**

## 次にやること（2026-11-09 終了時点）
1. `--dist/--yaw/--pitch` を CLI 化し、参照 `frames/v_0062.png` と同条件で A/B（縦横比 0.40±0.15 が目標）
2. VM#2 `0x5F`(1,692 回) / `0x29`(56 回) の解読
3. 合成（加算/α）と Z 順の確定（重なりで白飛びする）
4. 粒子サイズへの VM Scale 適用
5. VM#1 `0x1F`（円運動）/ `0x41`（ランダム方向）の移植

## 注意
- ゴールデン表が `docs/VERIFY-PLAN-*.md` などに**旧値のまま**残っている（更新は次回まとめて）
- `--geo on` は 0x1F のみ描くモード。α=0 の generator は空白になる（仕様）
- ビルボードの α=1 強制（色未設定時の代替）は**未検証**。`colorSet` フラグ導入は設計変更扱い

