# Microsoft OAuth同意後の接続テスト

## 報告と調査

- 個人用Microsoftアカウントで同意後、ブラウザに`Return to SmoothMail`が表示されたが、アプリ側の進行が分からないとの報告。
- この表示はcallback受領であり、コード交換・refresh・IMAP/SMTP認証成功の表示ではない。
- 従来はSMTP/IMAPが同一プロバイダ・同一ユーザーでも、SMTPの認可完了直後にIMAPの認可を別途開始していた。どちらのプロバイダも両プロトコルの権限を一括要求するため、同一ユーザーの二重認可は不要。二度目のブラウザ起動中も同じ案内が表示され、進行を判別できなかった。
- 実アカウントのtoken応答やエラーは未取得。この二重認可が報告された停止の唯一の原因だと断定せず、実サービスでの再試験を要する。

## 変更

- 新規認可の結果を、同じプロバイダ・ユーザーで未認可のもう一方のプロトコルにも設定する。異なるユーザー／プロバイダ、既存の認可は置き換えない。
- callback検証完了時に秘密情報を含まないsignalを通知し、UIを「認可結果受信・トークン取得中」に変更する。再認可ダイアログにも反映。
- refresh・接続テスト・プロトコル別OS資格情報保存は従来どおりで、認可だけで接続成功にはしない。
- PKCE・state/Host検査・TLS証明書検証・パスワードfallback禁止は維持。

## 検証

- Debugビルド成功。通常CTest 12ターゲット成功（OS資格情報テストは通常環境のopt-inなしで一部skip）。
- UI回帰テスト追加：Google/Microsoftで同一ユーザーは一度の認可から接続テストへ進行、接続失敗時の表示と再試験、異なるユーザー／プロバイダの分離、取消後の遅延応答拒否。
- OAuth callback通知の回帰テスト追加。制限内ではlocalhost接続が`ConnectingState`のままでcallback試験2件が失敗。差替えHTTPによるrefresh/登録エラー処理等は成功。制限外の実行は利用者が拒否し、Windows Sandboxでの試験を指定。
- 外部ネットワーク無効のWindows Sandboxで、専用テスト配置のみread-only共有し、専用出力フォルダへログを回収。OAuth 8 passed、UI 11 passed、0 failed / 0 skipped、両EXEのexit code 0。guestの使い捨て資格情報ストアを使うローテーション試験も成功。ログは`build/auth02-sandbox-output/`。Sandboxは終了済み。ホスト資格情報・DB・アプリ登録情報は共有／変更していない。
- 修正版起動フォルダ：`build/auth02-debug/start-smoothmail.cmd`。既存の配置・ランチャー・登録情報は未変更。Microsoft client IDを設定した起動環境から利用する。
- Microsoft実認証・実メール操作・OS保存復元の完了を意味しない。AUTH-02は継続。
