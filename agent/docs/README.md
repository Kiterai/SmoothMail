# 人向けドキュメント整理時の引継ぎ

`spec/docs.md`（e5bf4ad）に従い、`docs/` は利用方法・ビルド・テスト・実行・配布の5文書へ集約した。このディレクトリには、再調査や未完了仕様の根拠として必要な旧 `docs/dev/` の技術情報・検証記録を移した。日付・コミット・DB版・ログ・テスト件数は各記録時点の値であり、現在の実行手順ではない。

## 参照先

- 通常の手順：`../../docs/usage.md`、`../../docs/dev/`。
- 現在の未完了範囲：`../../spec/inprogress/README.md`。
- 個別依存の構築・パッチ・認証設計：`dependencies.md`、`oauth.md`。
- 旧tech.mdの実装構成・安全上の注意と情報の移管先：[技術引継ぎ](../tech-implementation-notes.md)。
- 配布監査の根拠：`distribution-legal-audit.md`、`distribution-path-policy.md`、`clean-dependency-verification.md`。
- 障害の再調査：`sync-diagnostics.md`、`crash-investigation-20261113.md`、`list-duplicates-20261114.md`。
- 検証・配備の根拠：`gui-checks.md`、日付・変更名別の各記録。Google通常GUIと同期障害の利用者確認は `manual-validation-20261114.md`。

## 削除・統合した内容

- `account-setup.md`、`settings-and-undo.md`：現行の操作を `docs/usage.md` へ統合。
- 旧 `build.md`：初期実装の「通信等が未実装」「MIMEを無効化可能」など、現コードと矛盾する記述を削除。統合入口 `cmake/BuildWindows.cmake` を基準に書き直した。
- `windows-launch.md`：再配置手順を `docs/dev/run.md` へ統合。重複した旧起動先・配備履歴は削除（個別の修正記録とGitに残る）。
- `implementation-status.md`：累積した古い状態説明を削除。現行の未完了仕様と個別の完了記録を正とする。
- `distribution-source-delivery.md` と旧 `distribution.md`：現行の準備・検証・公開・許諾上の注意を `docs/dev/distribution.md` へ統合。過去の検査停止原因・分岐別試行は既存の `agent/dist04-*`、`agent/integrated-runtime-builder-info-audit.md`、`agent/kde-runtime-path-patches.md` 等とGit履歴を参照する。

## 照合したGit履歴

- a2be6dd／7e6745c：Windows統合ビルド、CMake/C++への移植。古いPython直接実行の説明より優先。
- ebc06ab／ac6a25a／58705a0／42d8e44／956f60b：接続テストへの認可統合、対象指定更新、並列更新、返信元移動、細い境界線。
- 567d902／821da41／01271fd：情報ダイアログ、同ページでの対応ソース提供、Qt通知・ソース展開検証。
- d583f19／8d7b8cd／63585c2：供給元パスの限定許容、空キャッシュ取得完了。legal付き最終ZIPの完了とは区別する。

資料の移動に合わせて、参照元リンク、legalレビュー収集元、ソースレビュー出力の案内も更新した。レビュー出力の `REVIEW.md` には現行の `docs/dev/distribution.md` を使い、配布用ソースに含まれない `agent/` へ生成処理を依存させない。履歴資料自体を配布用ソースへ一括追加しない。

## 整理後の検証

- `docs/` のファイル集合が `spec/docs.md` の5ファイルと一致。
- `docs/`・`spec/`・`agent/` のMarkdownローカルリンクの参照先が存在。
- `git diff --check` と `cmake -P tests/cmake/run_build_guards.cmake` が成功。
- 既存Release補助ツールによるCTest 4対象（Qt SBOM、配布、ビルド、配布入口ガード）がすべて成功。
- `cmake/PrepareWindowsLegalReview.cmake` を既存39ファイルの候補へ実行し、19コンポーネントを `build/docs-reorganization-legal-review` へ収集。`REVIEW.md` と現行配布手順のSHA256一致を確認。
- アプリの再ビルド、GUI・実通信の再試験、最終配布ZIPの生成は行っていない。
