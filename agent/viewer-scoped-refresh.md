# 表示・対象指定更新の実装

- `displaytime.h` は `QTimeZone::systemTimeZone()` へ明示変換し、略語（取得不能時はUTCオフセット）を付加。曜日は日本語ロケール。技術ログのUTCは変更しない。
- MessageViewerの基本情報だけエスケープしたRichTextで装飾。本文／ヘッダは文字列のまま。内部縦Splitterを追加し、ヘッダトグルを2/3サイズの+/-へ変更。
- 検索位置をキャッシュし、可視範囲だけExtraSelectionを作成する。スクロール時に再生成。選択カーソルは一致開始に置き、通常の選択色でピンク／青の背景を上書きしない。
- `showLoading` / `showFailure` は古い基本情報・添付・ヘッダ・検索結果を破棄。本文読込応答はMainWindowでaccount/folder/UIDVALIDITY/UID/storageFolderの一致を確認してから反映。
- targetRefreshRequestedは空folderならaccount scope。それ以外はfolderOnlyをRequestに保持。queueの同一判定はaccount＋scope＋folderで、同じ対象の全更新だけ優先する。SyncModeの既存値0～2を維持しSelectedRecent=3を追加。
- フォルダscopeではpendingFolders/pendingMovesを送らず、LIST後のSTATUS/SELECT/fetchを選択対象に限定。serverFolderでローカル名を物理名へ解決する。commitSyncのfolderOnlyで別フォルダのmetadata/message/index/pending_flags/unreadを変更しない。
- GUI controller回帰は事前に2アカウントの各folder_stateを全取得で用意する。initialFetchRequired由来の正当な初回取得を誤って広域更新の漏れと判断しない。同期完了はMailService::synchronizedで数える（KIMAP Session::closeはLOGOUTを送らない）。

## 検証資材

`build/view-scope-check/` にruntime（42ファイルのSHA256 inventory）、tools（12 EXEのrunner）、symbols、outputを保存。既存SDKのDebug依存だけを使い、StageWindowsRuntime.cmakeの空destination要件を維持する。

最終結果：`output/final/` の全12 EXE Totalsを集計し193 passed / 0 failed / 0 skipped、failures.txt空を確認。ホストCTest15対象とPython31件も成功。初回output直下のGUI失敗はLOGOUTを期待した試験側の誤りで、synchronized通知で検査するよう修正して全12 EXEを再実行した。結果集計用の一時ワンライナーにも文字列エスケープの誤りがあったが、修正した独立集計で全Totalsとfailure markerを確認した。

Sandbox `1c689546-3e0d-4cca-84b8-8069cb1630c8` は終了成功。runtime inventory全42ファイル、全テストEXEとbuild/dev、配備EXEのSHA256を確認。SmoothMailが起動していないことを確認して旧local-runを`build/local-run-before-view-scope`へ移し、検証済みruntimeを配備した。`.agent-config/`は利用者管理の未追跡状態を維持し、内容に触れない。
