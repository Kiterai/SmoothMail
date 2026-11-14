# ライセンス情報ダイアログ

2026-11-14。仕様`71ea2a1`に従い実装。

- ヘルプ→このソフトウェアについて...。アカウント未登録でも利用可能。
- ソフト名/版、ビルド日（UTC、SOURCE_DATE_EPOCH対応）、ソースcommitを表示。変更済みソースはdirtyを明記。
- 左に項目、右に読取り専用プレインテキスト。HTML/JavaScriptや外部URLの自動実行はない。
- 法務manifestのコンポーネント別通知・原文をQt resourceへ埋め込み、オフラインでも閲覧できる。
- 開発ビルドはreview資料を明示表示可能。原文不足は未収集と表示し、監査済みと偽らない。
- 配布ビルドはreview-only資料・legal未指定・原文不足・dirty/不明なソース識別子を拒否。
- PackageWindowsから承認済みLEGAL_DIRをアプリ構築へ引き渡し、別々のライセンス集合にしない。
- 通常起動runtimeは変更していない。

検証:

- Debugのaboutdialog/mainwindow両CTest対象成功。
- 独立したReleaseレビューbuildでホスト12対象すべて成功。
- リスト切替・非空原文・MIT/変更通知、左右分割、読取り専用、版情報、メニュー起動・重複防止・終了を合成試験で確認。
- ログ`build/license-dialog-debug-build.log`、`build/license-dialog-tests.log`、
  `build/license-dialog-release-build.log`、`build/license-dialog-release-tests.log`。

最終原文・対応ソース・実配布の監査は引き続き必要。レビュー用バイナリを製品配布物としない。
