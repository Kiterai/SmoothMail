# 実装状況

現仕様の完成には至っていません。実装と検証を区別して管理します。

## 実装済み

- 3ペイン画面、メニュー、トレイ常駐、テキスト/HTMLソース表示、ヘッダ折り畳み、安全なURL装飾。
- アカウント追加、接続テスト、設定のSQLite保存、秘密情報のQtKeychain保存。平文フォールバックなし。
- IMAPフォルダ/メール取得、未読集計、一覧ソート、返信参照によるスレッド表示。
- F5/30秒更新、更新中表示、ローカルキャッシュ、UIDVALIDITY変更/削除のsnapshot同期。通信/MIME/DB処理はGUI外。
- 新規/返信作成画面、SMTP送信、IMAP下書き保存。BCCはSMTP宛先には含め、送信ヘッダからは除外します。下書きフォルダを特定できない場合は失敗を報告します。
- アカウントのドラッグ並び替えと保存、作成画面の入力欄に対する編集メニューのundo/redo。
- 添付ファイルのDownloads保存、同名ファイルの非上書き、履歴の永続化、元メッセージ/保存フォルダへの移動。
- Google/MicrosoftのOAuth認可/更新/失効の処理とUI・秘密情報保存・XOAUTH2認証への接続（模擬環境で検証、実サービスは未検証）。
- KIMAP/KSMTP/KMime/SASLとQtKeychainの指定Qt SDK向け構築。KIO/KIMAP/KSMTPのローカル変更はライセンス通知に記載。変更済みDLL・patch・Qt SDK・構成の取り違えをconfigure時に拒否。

## 検証済み（2026-11-13）

- 最新Debugの全15テストEXEをSDK未導入のWindows Sandboxで実行し、145 passed / 0 failed / 0 skipped（init/cleanup含む）。ログは`build/sandbox-mail-output/all-tst_*.txt`、`all-exit-code.txt=0`。
- 変更済み依存のreceipt検証はPythonの6テストで成功。DLL・Qt・patch・構成・commitの不一致と実行形式のreceiptを拒否。
- guest実GUIで起動、設定メニュー、アカウント追加の入力エラーと取消、空の履歴、閉じる操作後のトレイ常駐とダブルクリック復帰を確認。低解像度/拡大表示時の追加画面下端切れを修正し、決定/取消が画面内に収まることを再確認。
- 空または不正なReply-Toが存在するときはFromへ切り替えず宛先を空に保ち、送信前の補完を要求。Reply-Toがない場合だけFromを初期値にする回帰テストを追加。
- 開発用起動フォルダ`build/launch-debug`をwindeployqtと再帰import解決で作成。SDK/PATHなしの新しいguestでGUI起動と配置済み依存による合成通信14ケース成功。起動は`start-smoothmail.cmd`。手順・制限は`windows-launch.md`。
- 明示許可されたSafe Shell外でGoogle IMAP 993 / SMTP 465 / STARTTLS 587へ、資格情報なしで証明書検証付きTLS 1.3接続とXOAUTH2広告を確認。アカウントの認証・送受信は未実施。
- 提供されたGoogle OAuthクライアントIDで実認可を開始し、外部ブラウザを起動。ログイン後、利用者から「テスターのみアクセス可」の403 access_denied表示が報告されました。Google CloudのAudience/Test users設定が必要で、この実行では認可コード交換・トークン取得・メール認証へ進んでいません。待機中のプローブは停止済み。実サービス成功とは扱いません。

## 残る作業

- Google/Microsoftでの実認可・実メール送受信の検証。アプリ登録情報と利用者の認可が必要です。
- 配布物の依存/ライセンス監査とクリーン環境検証。

## 外部設定・権限

- Googleのテスト用アカウントとOAuth Desktop appのクライアントIDは提供済みです。利用者自身によるブラウザ認可と、その後のトークン取得・更新・メール認証の成功確認は必要です。Microsoftのアプリ登録・認可は未提供です。通常のGoogleパスワードをIMAP/SMTPへ送信する代替方法は使いません。利用者のパスワードやトークンをチャットへ貼り付ける必要はありません。
- Safe Shell内のloopback TCPはWinError 10013で拒否されます。外部通信を無効にしたWindows Sandbox内の合成サーバーではテスト可能です。
- OS資格情報ストアの書込みテストは使い捨てのWindows Sandbox内だけで実行しています。

検証の詳細・ログは`dependencies.md`と`agent/development.md`を参照してください。
