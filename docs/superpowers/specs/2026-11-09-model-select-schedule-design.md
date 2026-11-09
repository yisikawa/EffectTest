# モデル選択 → Schedule リスト 設計書（v2）

> 対象: `d:\projects\EffectTest`（EffectTest / Win32 + D3D11）
> 日付: 2026-11-09 / ブランチ: `design/list-to-screen`
> 位置づけ: `docs/DESIGN-List-To-Screen.md`（v1）に対する**差分設計**。UI と選択フローの正本は本書。
> 制約: 設計段階のためコードは変更しない（実装はユーザーの明示指示後）。実測値は実コード/実 DAT に基づく。

## 0. 決定事項

| # | 決定 | 根拠 |
|---|---|---|
| D1 | **モデルの単一選択（クリック / キー移動）で即時に** DAT を読み、Schedule 一覧を更新する。ダブルクリックは同義（後方互換） | ユーザー回答「単一選択で即時」 |
| D2 | 3 段目は COMBOBOX ではなく **LISTBOX**（常時見える一覧） | ユーザー回答「LISTBOX で常に見える一覧にしたい」 |
| D3 | 用語は **Schedule** に統一（= モデル DAT 内の `0x07` ブロック。AltanaView の `[NPC] Schedule` と同義）。v1 の「Action」はその別名 | `AltanaView.ini` の `Schedule=` / 計測（`ROM\131\6.DAT` の `0x07`=39 件） |
| D4 | 選択の連続発火に備え **120 ms デバウンス**＋確定 index ガード | 1,266 件リスト（R4）でのキー連打耐性 |
| D5 | Schedule は**入れ替え**（蓄積しない）。常に「現在選択中の 1 モデル分」 | Schedule はモデル DAT 内の資源であり、他モデルと混在させると解決先が曖昧になる |
| D6 | Schedule の再生は**リストの選択（`LBN_SELCHANGE`）で即再生**（現行 combo の挙動を踏襲）。`R` でリセット、`Space` で一時停止 | 既存操作（`App.cpp:469` / `:453`）との連続性 |

## 1. 目的

- AltanaView と同じ操作感にする: **モデルを選ぶ → そのモデルの Schedule が一覧に出る → 選ぶと再生**。
- 現行の不整合を解消する: 3 段目は**ダブルクリックするまで空**（`App.cpp:622-641` は `LBN_DBLCLK`（`:693-696`）からのみ到達）。単一選択では何も起きないため、利用者から見て「選んでも Schedule が出ない」状態になっている。

## 2. 用語

| 用語 | 定義 |
|---|---|
| グループ | List のフォルダ相当。`NPC ROM1`〜`ROM9` / `Effect: <カテゴリ>`（29 グループ） |
| 項目 | グループ内の 1 行。NPC ならモデル（`名前 (1-131-6)`）、Effect ならエフェクト（`Fire (11/17)`） |
| **Schedule** | モデル DAT 内の `0x07` ブロック（`atk0`, `cast`, `vatk` …）。AltanaView の Schedule コンボと同一。v1 設計書の「Action」＝本書の Schedule |
| Motion | 骨モーション（`0x2B`）。AltanaView の Motion コンボ相当。**今回の非対象** |
| 確定 | 選択中の項目に対し DAT を読み込み、Schedule 一覧と表示を更新した状態 |

## 3. UI 仕様

### 3.1 コントロール

| コントロール | ID | 型 | 内容 | 有効条件 |
|---|---|---|---|---|
| グループ | `IDC_GROUP` = 1001 | `COMBOBOX`（`CBS_DROPDOWNLIST`） | 29 グループ | 常時 |
| 項目 | `IDC_ITEMS` = 1002 | `LISTBOX`（`LBS_NOTIFY`） | `名前 (ID)` を列挙（最大 1,266 件） | 常時 |
| Schedule ラベル | `IDC_SCHEDULE_LABEL` = 1004 | `STATIC` | `Schedule (39)` のように**件数**を併記 | 常時 |
| **Schedule** | `IDC_SCHEDULE` = 1003 | `LISTBOX`（`LBS_NOTIFY｜WS_VSCROLL｜LBS_NOINTEGRALHEIGHT`） | 選択モデルの `0x07` 名 | NPC 確定時のみ。Effect 確定時／未選択時は `EnableWindow(FALSE)` |

- `IDC_ACTION`（1003）は**値を据え置き**、名前だけ `IDC_SCHEDULE` に変更する（差分を小さく保つ）。
- 内部名も合わせる: `g_hAction`→`g_hSchedule`、`g_ActionNames`→`g_ScheduleNames`、`PlaySelectedAction`→`PlaySelectedSchedule`、ログ接頭辞 `[action]`→`[schedule]`。

### 3.2 レイアウト（`LayoutPanel`）

`w = クライアント幅 - 16`、`bottom = クライアント高` として:

```
g_hGroup     : x=8, y=8,             w, h=200        … ドロップダウン展開高
g_hItems     : x=8, y=32,            w, h=bottom-194 … 残余をすべて項目に与える
g_hScheduleLabel : x=8, y=bottom-158, w, h=18
g_hSchedule  : x=8, y=bottom-136,    w, h=128         … 6〜7 行が見える高さ
```

- 高さが不足する場合（`bottom < 320`）は Schedule を 96、項目の最小高を 64 とし、負値を作らない。

### 3.3 表示文字列

| 状態 | ラベル |
|---|---|
| 未選択（起動直後） | `Schedule (-)` |
| NPC 確定（39 件） | `Schedule (39)` |
| NPC 確定（0 件） | `Schedule (0)` |
| Effect 確定 | `Schedule (-)`（リストは空・無効） |

## 4. イベントとデータフロー

### 4.1 モデル選択（単一選択 → デバウンス → 確定）

```
WM_COMMAND / IDC_ITEMS / LBN_SELCHANGE または LBN_DBLCLK
    g_PendingItem = (int)SendMessageW(g_hItems, LB_GETCURSEL, 0, 0)
    KillTimer(hPanel, IDT_COMMIT)                     … 既存の待ちを破棄
    SetTimer(hPanel, IDT_COMMIT, 120, nullptr)        … 120 ms 静止後に 1 回だけ

WM_TIMER / IDT_COMMIT
    KillTimer(hPanel, IDT_COMMIT)
    CommitSelection(g_PendingItem)
```

### 4.2 `CommitSelection(index)`（中核・唯一の確定経路）

```
if (index < 0 || g_CommitBusy) return;                 // 再入防止
if (index == g_CommittedItem && グループも同一) return;  // 同一項目の再確定は no-op

g_CommitBusy = true;
  グループ g = g_List.Group(gi); 項目 it = g_List.Items(gi)[index];
  if (!ListDb::ResolvePath(...))            → ClearSelection("resolve failed", it.id); goto done;
  if (!LoadDatFile(path))                   → ClearSelection("load failed", path);     goto done;
  if (g.isEffect) {                          // Effect: Schedule は対象外
      ScheduleClear();  EnableWindow(g_hSchedule, FALSE);  ラベル "Schedule (-)"
      g_Effect.Reset();  g_Paused = false;                 // 即再生（従来どおり）
  } else {                                   // NPC: Schedule を列挙
      g_ScheduleNames = g_Dat の 0x07 名（チャンク順）
      ScheduleClear(); for each name → LB_ADDSTRING(名前)
      if (!empty) LB_SETCURSEL(0)             // 先頭を自動選択（※ 再生はしない）
      EnableWindow(g_hSchedule, TRUE);
      ラベル "Schedule (<件数>)";  ログ "[schedule] count=<n>"
  }
  g_CommittedItem = index;  g_CommittedGroup = gi;
  UpdateTitle();
done:
g_CommitBusy = false;
```

**再生しない理由（自動選択時）**: `LB_SETCURSEL` は `LBN_SELCHANGE` を送出しないため、先頭の自動選択で勝手に再生が始まることはない。利用者が明示的に選んだ行だけを再生する（D6）。

### 4.3 Schedule の選択（再生）

```
WM_COMMAND / IDC_SCHEDULE / LBN_SELCHANGE または LBN_DBLCLK
    PlaySelectedSchedule((int)LB_GETCURSEL)
        index 範囲外 / g_Dat 未ロード → 何もしない
        g_ActionCtx.BuildFromChunk(g_Dat, name) 失敗 → ログ "[schedule] <name> : timeline 無し"
        成功 → g_ActionCtx.Reset(); g_ActionActive = true; ログ（命令列のダンプは従来どおり）
```

### 4.4 後始末 `ClearSelection(reason, detail)`

```
g_Effect.ReleaseTextures();   // SRV 解放（Effect.cpp:193）
g_Effect.Reset();             // インスタンス消去（Effect.cpp:203）
g_ActionCtx.Reset();  g_ActionActive = false;
ScheduleClear();  EnableWindow(g_hSchedule, FALSE);  ラベル "Schedule (-)"
g_Dat.Unload();               // チャンク索引も破棄（Dat.h:34、public）
g_DatPath.clear();  g_CommittedItem = -1;  g_CommittedGroup = -1;  g_PendingItem = -1;
UpdateTitle();                // 「List から項目を選択してください」へ戻る
ログ "[schedule] clear: <reason> <detail>"
```

> `DatFile::Load` は先頭で `Unload()` を呼ぶ（`Dat.cpp:8`）ため、読み込み失敗時に古い DAT は残らない。
> `ClearSelection` では残存索引を残さないよう、public な **`DatFile::Unload()`（`Dat.h:34`）** を明示的に呼ぶ。新しい API は追加しない。

## 5. 状態モデル

| 状態 | 項目リスト | `g_DatPath` | Schedule リスト | 画面 |
|---|---|---|---|---|
| 未選択（起動直後） | 表示 | 空 | 空・無効 | 背景のみ（座標軸は既定 OFF） |
| Effect 確定 | 表示 | `…\ROM\11\17.DAT` | 空・無効 | エフェクト再生 |
| NPC 確定 | 表示 | `…\ROM\131\6.DAT` | 39 件・有効／先頭選択 | エフェクト無し（モデル描画は P1 の非対象） |
| 失敗 | 表示 | 空 | 空・無効 | 背景のみ＋タイトルは案内文 |

## 6. 失敗・空の扱い

| 事象 | 挙動 |
|---|---|
| `ResolvePath` 失敗（ROM 未インストール等） | `ClearSelection("resolve failed", id)`。**直前の表示を残さない**（起動時規約「何も表示しない」と整合） |
| DAT 読み込み失敗（破損・0 バイト） | `ClearSelection("load failed", path)` |
| `0x07` が 0 件 | 正常系。ラベル `Schedule (0)`、リスト有効・空、ログ `[schedule] count=0` |
| Schedule チャンクがタイムラインを持たない | ログのみ。再生位置は変化させない |
| ベース `ROM\0\0.DAT` 読込失敗 | 従来どおり警告のみで起動継続（v1 §7 R7） |

## 7. 性能・再入防止

| 対策 | 内容 |
|---|---|
| デバウンス | `SetTimer` 120 ms。↑↓ キー長押しや連続クリックで DAT 読みが発火しない |
| 確定 index ガード | 同一グループ・同一 index の再確定は no-op（ダブルクリックの 2 回目を吸収） |
| 再入フラグ | `g_CommitBusy` で `CommitSelection` の多重実行を抑止 |
| タイマ破棄 | グループ変更時に `KillTimer`（`FillItems` 内） |

**目標値**（検証計画で実測）

- 単一選択 → Schedule 反映: **< 50 ms**（ROM1 の最大 DAT、`load_ms` をログ出力）
- 1,266 件リストで ↑↓ 長押し 2 秒: フレーム落ち（> 16.7 ms）が 5% 未満

## 8. 既存コードへの影響（`App.cpp` のみ）

| 箇所 | 現状 | 変更 |
|---|---|---|
| `enum`（`:80`） | `IDC_GROUP/IDC_ITEMS/IDC_ACTION` | `IDC_ACTION`→`IDC_SCHEDULE`（値 1003 維持）、`IDC_SCHEDULE_LABEL=1004`、`IDT_COMMIT=1` を追加 |
| グローバル（`:64-75`） | `g_hAction`, `g_ActionNames` | `g_hSchedule`, `g_hScheduleLabel`, `g_ScheduleNames`, `g_PendingItem`, `g_CommittedItem`, `g_CommittedGroup`, `g_CommitBusy` |
| `BuildPanelControls`（`:537`） | COMBOBOX 1 個 | LISTBOX＋STATIC ラベルに置換、初期は `EnableWindow(FALSE)` |
| `LayoutPanel`（`:524`） | 3 要素 | §3.2 の式へ |
| `FillItems`（`:575`） | `CB_RESETCONTENT` 等 | グループ変更で `KillTimer`、Schedule を空・無効化、`g_Committed*` をリセット |
| `LoadListSelection`（`:599`） | 単一の確定処理 | `CommitSelection` に改称し §4.2 の分岐へ。Effect/NPC の分岐は現行ロジックを流用 |
| `PlaySelectedAction`（`:648`） | `g_ActionNames` 参照 | `PlaySelectedSchedule` に改称。中身は同一 |
| `PanelProc`（`:686-704`） | `LBN_DBLCLK` のみ | `LBN_SELCHANGE` を追加（デバウンス）、`IDC_SCHEDULE` の `LBN_SELCHANGE`／`LBN_DBLCLK`、`WM_TIMER` を追加 |
| 新規ヘルパ | — | `ScheduleClear()`, `ClearSelection()`, `UpdateScheduleLabel()`（3 関数、合計 30 行程度） |

- 変更しない: `Dat.*`, `Effect.*`, `EffectVm.*`, `ListDb.*`, `Tim.*`, `Gfx.*`, `EffectBase.*`
- 検証モード（`--list` / `--base` / `--action` / `--selftest` / `--shot`）の**引数仕様は不変**。`--action` の出力書式も不変（実装計画で回帰確認）。

## 9. v1 設計書（`docs/DESIGN-List-To-Screen.md`）への反映

| 節 | 変更 |
|---|---|
| §0 用語 | **Schedule** を追加し、v1 の「Action」をその別名と明記 |
| §1 表示の規約 | トリガーを「**項目の単一選択（クリック）**」へ変更。ダブルクリックは同義 |
| §4.1 / §4.2 / §4.3 | 3 段目を **Schedule（LISTBOX）** に、`項目 dblclick` を `単一選択` に改める |
| §5 P1 / P2 | 受け入れ基準に「選択直後に Schedule が埋まる（`ROM\131\6.DAT` = 39 件）」を追加 |
| §8 | 「単一選択トリガー」「Schedule リストの常時表示」を決定済みとして記録 |

- 実装時には `ReadMe.md` の操作説明（「ダブルクリックで確定」→「単一選択で確定」、「Action」→「Schedule」）も更新する。

## 10. 非対象（YAGNI）

- モデルのメッシュ／スケルトン／テクスチャ描画（v1 P1。土台アプリ案 D/C の決定後）
- Schedule の**蓄積**（複数モデル分を 1 つのリストに溜める）／再生キュー
- Motion（`0x2B`）の一覧と選択（AltanaView の Motion コンボ相当。別課題）
- Schedule→Effect の紐付け（v1 §8-3 のとおり当面なし）
- `Play` / `Stop` ボタンの追加（キー操作 `Space`/`R` で代替。必要になったら追加）

## 11. 検証方針

| 層 | 手法 | 判定（ゴールデン） |
|---|---|---|
| List 解析（回帰） | `--list` | 29 グループ／NPC ROM1 = 1,266 件（従来値と一致） |
| Schedule 列挙（新規の観測点） | 実装計画で **`--schedule <dat>` を追加**し、`0x07` 名を 1 行 1 件で出力（既存 `--action` の出力書式は不変のまま回帰確認に使う） | `0x07` = **39 件**、`atk0` の命令 6 件（`hwat` 不一致／`vatk` は `0x07`） |
| Effect（回帰） | `--shot "…\ROM\11\17.DAT" out.png` | timeline=18 / spawn=8 / 3.3333 s、非背景 > 6,000 サンプル |
| UI 手動 | グループ `NPC ROM1` → `ヒューム♀NPC (3-11)` を**シングルクリック** | Schedule ラベルが `Schedule (n)` になり、リストに `0x07` 名が並ぶ |
| UI 手動（失敗系） | 存在しない ROM グループ、壊れた DAT | 画面が背景のみに戻り、ログに `clear:` が出る |
| 性能 | ↑↓ 長押し 2 秒／連続クリック | §7 の目標値を満たす |

## 12. リスク

| # | リスク | 対応 |
|---|---|---|
| S-R1 | 単一選択の即時ロードで UI が固まる | デバウンス＋実測（§7）。改善しない場合は案 C（0x07 名の軽量索引キャッシュ）へ |
| S-R2 | キー移動で再生中のエフェクトが頻繁に切り替わる | 仕様（D1/D5）。必要なら「Effect のみデバウンス長を 200 ms」に調整 |
| S-R3 | Schedule リストの高さ不足（小さいウィンドウ） | §3.2 の縮退規則 |
| S-R4 | v1 設計書と本書の記述の二重管理 | 本書を UI/フローの正本とし、v1 は §9 の差分のみ反映 |


