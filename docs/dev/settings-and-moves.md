# 設定・移動・Undoの実装メモ

- SQLite v5へ自動移行。取得済みフォルダ、メール移動の保留、UID追従用ハンドル、履歴のMIMEタイプを追加します。旧版へのDBダウングレードは行いません。
- 本文は移動の保留中も元の場所に保存し、一覧に移動先を重ねます。フォルダ・UIDVALIDITY・UIDだけでなく物理ソースも含めて識別します。
- サーバへの移動はKIMAPのMOVEを使用。成功後のUID対応付けで、以前のフラグ操作とUndoの参照先を更新します。応答喪失時はソースの有無を確認してから再取得します。
- Undo履歴はメモリー上、保留操作はDB上です。自動既読は履歴へ入れません。非同期操作に失敗した場合はエラーを表示し、履歴を自動で巻き戻しません。
- 設定の公開値とOAuthシークレットは保存先が異なり、単一トランザクションにはできません。失敗時は復元を試み、失敗を通知します。

回帰テストは`tests/tst_accountrepository.cpp`、`tests/tst_connectiontester.cpp`、`tests/tst_mainwindow.cpp`、`tests/tst_messageviewer.cpp`、`tests/tst_guismoke.cpp`。実keychainと通信の検証は使い捨てのオフラインWindows Sandboxで行います。
