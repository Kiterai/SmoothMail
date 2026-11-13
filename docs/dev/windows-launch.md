# Windows開発用の起動設定

現在の起動用フォルダは`build/local-run`です。
**通常の`build/start-smoothmail.cmd`は仕様803f145対応の新版を起動します。** 利用者によるアプリ終了後に`build/local-run`を更新しました。旧ランタイムは`build/local-run-before-behavior`に保存しています。新DBはv3となり旧版では開けません。配置42ファイルのハッシュ・保存EXEとの一致を再確認済みで、配置ログは`build/behavior-deliver-stage.log`、EXE/PDBは`build/behavior-check/symbols/`です。詳細は`behavior-update.md`を参照してください。以下は従来の通常起動先の履歴です。
最新は再発対策の**KCodecs修正版**です。`build/start-smoothmail.cmd`から起動してください。直前版は`build/local-run-before-codecsfix`、最新配置ログは`build/codecs-check/stage.log`、対応するEXE/KCodecs/KMime DLL・PDBは`build/codecs-check/symbols/`です。42ファイルの配置ハッシュとビルド/保存成果物の一致を確認しています。EXEは前回と同じでもKCodecs DLLが更新されています。以下のKMime修正時の記載は履歴です。
2026-11-13のCRT異常終了対策（KMimeのunsigned-char修正と安全な診断強化）も反映済みです。直前のページ表示版は`build/local-run-before-ctypefix`へ保存しました。最新の配置ログは`build/local-run-crashfix-final-stage.log`、対応するEXE/KMime DLLとPDBは`build/crash-check/symbols/`です。配置42ファイルのハッシュ、現在のビルド/修正DLLとの一致、登録JSON・テストEXEの非混入を確認しています。調査結果は`crash-investigation-20261113.md`を参照してください。
**`build/start-smoothmail.cmd`をダブルクリック**して起動します。フォルダ内の`build/local-run/start-smoothmail.cmd`や`smoothmail.exe`からも起動できます。Qt SDKやKDE prefixをPATHへ追加する必要はありません。

今回の依頼ではWindows SandboxのGUI確認と手元起動用Debug環境だけを整備しています。Microsoft実アカウント、GUIでのGoogle認可、配布用Release設定は後回しです。新しい起動フォルダには登録JSON・秘密情報・GUIテストEXEを含めていません。以前の`build/launch-debug`とGoogle用ランチャーは上書きせず残しています。

このフォルダは**Debugの開発用**です。Debug CRTを含むため、第三者への配布物ではありません。Release配布・完全なライセンス監査は別途必要です。Qt SDKは変更しません。

大量メールの分割同期/ページ表示への変更後も`build/start-smoothmail.cmd`が最新の`build/local-run`を起動します。更新直前のランタイムは`build/local-run-before-paging`に保存しました。配置ログは`build/local-run-paging-stage.log`、対応するEXE/PDBは`build/paging-check/symbols/`です。全配置ハッシュ・最新EXEとの一致・登録JSON/テストEXEの非混入を確認しています。SQLiteキャッシュのv2移行と旧版への切戻しの注意は`large-mailboxes.md`を参照してください。

以前のUI仕様変更時の手元用フォルダは`build/local-run-before-ui-update`に保存しています。その時点の配置ログは`build/local-run-ui-update-final-stage.log`、EXE/PDBの対応保存は`build/ui-update-check/symbols/`です。新しい診断ログの場所と読み方は`sync-diagnostics.md`を参照してください。

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
 -DDESTINATION="%CD%/build/local-run-new" ^
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

以前の`build/launch-debug`には`start-google-dev.cmd`も用意しています。提供された公開クライアントIDを設定し、`build`配下の利用者指定Desktop登録JSONをローカルで読み込み、そのプロセスの環境変数にclient_secretを渡して起動します（両ファイルともローカルの`build`配下のみ）。今回はこれを変更・実行していません。GUIでのGoogle認可を再開する際は、最新EXEと設定を改めて揃えて検証してください。

## 確認記録

- 今回のGUI確認では、外部通信無効・SDK未導入のguestでネイティブGUIの4テストEXEが42 passed / 0 failed / 0 skipped。アカウント追加から合成送信/下書き/添付/履歴/30秒更新までを本体controller経由で確認しました。詳細は`gui-checks.md`。
- 修正後に`windeployqt`と再帰import解決で`build/local-run`を新規作成。さらに別のSDK未導入guestへこのフォルダだけをコピーし、`C:\SmoothMail Local`という空白入りパス、WindowsだけのPATHで起動を確認しました。`runtime-sha256.txt`の全ハッシュと最新EXEの一致を確認済みです。
- 実行環境に登録JSONやテストEXEが混入していないことを確認しました。ログは`build/local-run-stage.log`、実ロードDLL一覧は`build/gui-check/output/final-runtime-loaded-modules.txt`です。

以下は過去の開発用ランタイムでの記録です。

- 2026-11-13、SDK未導入の新しいWindows Sandboxに、この起動用フォルダだけをコピーし、PATHをWindowsのみにして`start-smoothmail.cmd`から起動できることを確認しました。
- 追加のテスト用EXEとQt Test DLLだけを別フォルダへ置き、メール/Qt/SASLの実行依存にはこの起動用フォルダだけを使って合成通信テストを実行。TLS/STARTTLS、証明書拒否、XOAUTH2、同期、SMTP送信・下書きの14ケースが成功しました（失敗・スキップなし、init/cleanup含む）。`build/sandbox-mail-output/runtime-probe-results.txt`に記録しています。実Googleアカウントの認証ではありません。
- 認証なしのGoogleサーバー確認（`tests/manual/google_readiness.py`）は、明示許可されたSafe Shell外で成功。IMAP 993、SMTP 465、SMTP STARTTLS 587で証明書検証付きTLS 1.3とXOAUTH2広告を確認しました。認証・メール送受信・このアプリでのOAuth成功を意味しません。
- 通常のSafe Shell内では同確認がWinError 10013で拒否されるため、外側で実行する場合は権限許可が必要です。スクリプトは資格情報を読み込まず、認証・メール送信を行いません。
