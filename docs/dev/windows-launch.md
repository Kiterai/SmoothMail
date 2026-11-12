# Windows開発用の起動設定

現在の起動用フォルダは`build/launch-debug`です。
`start-smoothmail.cmd`をダブルクリックして起動します。Qt SDKやKDE prefixをPATHへ追加する必要はありません。

このフォルダは**Debugの開発用**です。Debug CRTを含むため、第三者への配布物ではありません。Release配布・完全なライセンス監査は別途必要です。Qt SDKは変更しません。

## 再作成

x64 Native Tools Command Prompt（MSVC 2022）から、アプリをビルドした後に実行します。保存先は空のディレクトリを指定してください。既存ファイルを削除・上書きして再作成する動作はしません。下記はリポジトリルートからの例です。

```bat
cmake --build --preset dev
cmake ^
 -DAPP="%CD%/build/dev/smoothmail.exe" ^
 -DQT_BIN=C:/Qt/6.12.0/msvc2022_64/bin ^
 -DDEPENDENCY_BIN="%CD%/build/deps/install/Debug/bin" ^
 -DNATIVE_BIN="%CD%/build/deps/native/x64-windows/debug/bin" ^
 -DSASL_DIR="%CD%/build/deps/install/Debug/bin/sasl2" ^
 -DDESTINATION="%CD%/build/launch-debug-new" ^
 -DCONFIGURATION=Debug -P cmake/StageWindowsRuntime.cmake
```

- DLLのimportを`dumpbin`で再帰解決し、KDE/QtKeychain/SASL/native依存を特定します。prefix全体はコピーしません。
- `windeployqt`には実際に解決されたQt SDK DLLを渡します。Qt 6.12は`qt6keychaind.dll`をQt SDK内のモジュールと誤認するため、アプリEXEを直接渡す方法は避けます。SDKへQtKeychainをコピーしたり、不足DLLのエラーを無視したりしません。
- `qwindowsd`、`qsqlited`、`qschannelbackendd`とPLAIN/LOGIN/XOAUTH2を明示配置します。Qtの不要なSQLドライバ・KIO worker・補助EXEは配置しません。
- 選択したMSVCツールチェインのDebug CRT/UCRTを開発テスト用に配置します。OS DLLは配布対象にしません。
- `qt.conf`と起動用cmdでプラグイン/SASL探索先を実行フォルダへ固定します。OAuthの環境変数は起動元から引き継ぎます。パスワードやトークンをcmdへ書かないでください。
- `runtime-sha256.txt`は配置ファイルのSHA-256一覧です。署名や配布ライセンス適合の保証ではありません。

## Googleの実サービス検証の前提

テスト用GoogleアカウントだけではOAuthクライアントの代わりになりません。Google CloudでDesktop appのOAuthクライアントを作成し、テスト用アカウントを同意画面のテストユーザーに登録します。Gmail APIを有効にし、必要な`https://mail.google.com/`スコープを設定してください。登録手順はGoogleの公式資料を確認してください。

起動前に`SMOOTHMAIL_GOOGLE_CLIENT_ID`（必要なら公開Desktop登録情報の`SMOOTHMAIL_GOOGLE_CLIENT_SECRET`）を設定します。詳細は`oauth.md`を参照してください。アカウント追加ではSMTPを`smtp.gmail.com:465`のSSL/TLS、IMAP4を`imap.gmail.com:993`のSSL/TLS、双方の認証をOAuth2/Googleにし、それぞれ外部ブラウザで認可します。通常のGoogleパスワードはこれらの欄へ入力しません。

現在の開発用フォルダには、提供された公開クライアントIDを設定して起動する`start-google-dev.cmd`も用意しています（ローカルの`build`配下のみ、秘密情報は含めません）。起動用EXEには登録情報エラーの診断改善も反映済みです。

## 確認記録

- 2026-11-13、SDK未導入の新しいWindows Sandboxに、この起動用フォルダだけをコピーし、PATHをWindowsのみにして`start-smoothmail.cmd`から起動できることを確認しました。
- 追加のテスト用EXEとQt Test DLLだけを別フォルダへ置き、メール/Qt/SASLの実行依存にはこの起動用フォルダだけを使って合成通信テストを実行。TLS/STARTTLS、証明書拒否、XOAUTH2、同期、SMTP送信・下書きの14ケースが成功しました（失敗・スキップなし、init/cleanup含む）。`build/sandbox-mail-output/runtime-probe-results.txt`に記録しています。実Googleアカウントの認証ではありません。
- 認証なしのGoogleサーバー確認（`tests/manual/google_readiness.py`）は、明示許可されたSafe Shell外で成功。IMAP 993、SMTP 465、SMTP STARTTLS 587で証明書検証付きTLS 1.3とXOAUTH2広告を確認しました。認証・メール送受信・このアプリでのOAuth成功を意味しません。
- 通常のSafe Shell内では同確認がWinError 10013で拒否されるため、外側で実行する場合は権限許可が必要です。スクリプトは資格情報を読み込まず、認証・メール送信を行いません。
