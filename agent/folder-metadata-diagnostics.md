# フォルダ情報エラーの診断追加

## 対象とプライバシー

- 7af15b9の診断仕様と利用者の「更新に失敗しました: フォルダ情報が不正です。」に対応。
- 検証後のエラー分類は空名・同名重複・負の未読数。固定キーと数値だけで全条件の件数・最初の項目番号・重複元番号を記録。フォルダ名・アカウントID・アドレス・サーバ名・認証情報・本文・サーバ応答・それらのハッシュは記録しない。
- validFolderMetadataをsaveSnapshot/commitSyncで共用し、既存の拒否条件・表示メッセージは維持。名前はKIMAPによるデコード後。実際のサーバ事象の根本原因はまだ特定していない。

## 処理番号とエラー経路

- diagnostics::OperationScopeはスレッドローカルの番号をRAIIで設定・復元。nextOperationはatomic連番。ネットワークworkerからbeginSyncへ番号を渡し、State経由でDB workerのstage/reuse/commit/abortに継承。アカウントの永続的な匿名化IDではない。
- saveSnapshotは独立した番号を発行。通常eventの末尾にopを付け、フォルダ失敗も同じ番号で記録。
- saveSnapshot/commitSyncの失敗経路を共通failへ集め、早期returnでも失敗イベントと、トランザクション開始後ならrollback結果を記録。トランザクション未開始の失敗をrollback成功と誤記しない。
- SQL helper等はQSqlErrorの型と数値として解釈できるnative codeのみ記録。SQL文・lastError.textはUIには従来どおり返すがログへは書かない。
- Qt warning/criticalはカテゴリを固定allowlistへ分類し、ソース行番号のみ記録。任意のcategory/message/file/functionは書かない。ログ書込み中のQt警告による再入・mutexデッドロックをthread_local guardで抑止。既存のQt Test handlerは維持し、fatal/CRTの固定分類と処理は維持する。

## テスト

- Diagnostics：型付きフィールド、ネストしたScopeの復元、別スレッドへの番号受渡し、警告・critical原文／パス／任意カテゴリの非保存。
- Repository：snapshot/streamingの両方で空名・重複・負数・複合の8ケース。詳細ログ・番号・rollbackと既存キャッシュ保持、秘密情報非保存を検査。
- Transport：合成サーバがINBOXを重複LISTするケースで同期→DB失敗→同期失敗の番号一致と原文非保存、正常LISTへ戻した後の再試行成功を検査。

## 最終確認

- オフラインWindows Sandbox12 EXE：172 passed / 0 failed / 0 skipped。全Totalsとfailures.txtの空を確認。`build/folder-diagnostics-check/output/`。今回起動したSandbox1台は終了済み。
- ホスト非ネットワークCTest15対象とPython31件が成功。実アカウント・実メールを用いた再現は行っていない。
- ランタイム42ファイルのSHA256とビルド/配備/シンボル用EXEの一致を確認。`build/local-run`を更新し、旧版は`build/local-run-before-folder-diagnostics`へ退避。実行中プロセスの強制終了やSDK変更はしていない。
