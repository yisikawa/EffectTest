# 開発ワークフロー（記憶事項）

このリポジトリ（EffectTest）の開発ルール。全体ルールは
`D:\tools\AltanaView-master\.clinerules\00-workflow-memory.md` と同一。

## git
- **git 前提**。実装開始前に `git init` し、初回コミットに現状を記録する。
- 変更はブランチ＋小さなコミット（日本語メッセージ）。

## 進め方（順序厳守）
1. 設計書（コード変更禁止）
2. 実装計画（`docs\superpowers\plans\YYYY-MM-DD-<feature>.md`）
3. テスト計画
4. 検証計画
5. **ユーザーの明示指示後に実装**

## 検証
- `--list` / `--base` / `--action` / `--selftest` のヘッドレス出力をゴールデン値と比較
- 描画は `--shot` の PNG を画素検査
- ビルド: MSBuild v143 / Release / x64
