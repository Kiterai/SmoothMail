# Google / Microsoft OAuth

認証実装・外部設定・手動プローブの引継ぎ資料です。過去の試験経緯を含みます。通常の登録操作は[利用方法](../../docs/usage.md)、未検証範囲は[未完了仕様](../../spec/inprogress/live-authentication.md)を優先してください。

Qt Network上にAuthorization Code + PKCE S256 + state検証を実装しています。ブラウザはシステムの外部ブラウザを使用し、callbackはIPv4 loopbackのランダムポートに限定します。暗号化・乱数・SHA-256はQtの機能を利用します。

- Google: `https://accounts.google.com/o/oauth2/v2/auth`、`https://oauth2.googleapis.com/token`。スコープは`https://mail.google.com/`。callbackは`http://127.0.0.1:<port>/`。
- Microsoft: `https://login.microsoftonline.com/common/oauth2/v2.0/authorize` / `token`。スコープは`https://outlook.office.com/IMAP.AccessAsUser.All`、`https://outlook.office.com/SMTP.Send`、`offline_access`。callbackは`http://localhost:<port>/`。

エンドポイントは上記に固定し、HTTPSの証明書検証を無効にしません。トークンPOSTのredirectも追跡しません。state/codeの重複、不一致、Hostの不一致、不正callbackを拒否し、有効な認可コードだけを1回交換します。トークン応答サイズ・タイムアウトを制限し、失効は再認可が必要なエラーとして報告します。パスワード認証への自動切替はありません。

## 外部で必要な設定

GoogleではDesktop app、MicrosoftではMobile and desktop applicationsのpublic clientとしてアプリを登録してください。Microsoftのsystem browser redirect URIは`http://localhost`です。登録した対象アカウント種別と委任権限、組織側のIMAP/SMTP AUTH許可を確認してください。Googleのメールスコープは公開形態によって検証・審査が必要です。

Googleのテスト用アカウントとDesktop OAuthクライアントIDは提供済みです。実サービス検証では利用者自身による外部ブラウザのログイン・同意が必要です。認可後のトークン取得・更新・IMAP/SMTP認証まで確認して初めて成功とします。秘密情報をチャットへ貼り付ける必要はありません。Windowsの起動手順は[実行手順](../../docs/dev/run.md)を参照してください。

起動環境で以下を設定します。クライアントIDは公開アプリの識別子です。Googleのdesktop client secretが登録情報に含まれる場合だけ、任意で`SMOOTHMAIL_GOOGLE_CLIENT_SECRET`も設定できます（desktop appでは機密保持できる秘密として扱いません）。Microsoftはpublic clientでありclient secretを使いません。

```bat
set SMOOTHMAIL_GOOGLE_CLIENT_ID=<Google Desktop appのクライアントID>
set SMOOTHMAIL_MICROSOFT_CLIENT_ID=<Microsoft public clientのアプリID>
```

アカウント追加・編集で各プロトコルをOAuth2にしてプロバイダを選び、「接続テスト」を実行します。必要なOAuth認可もここから外部ブラウザで行います。SMTP・IMAP4の両方のテストが成功すると「決定」で保存できます。設定を変更すると再テストが必要です。認可専用ボタンはありません。更新トークンはプロトコル別のQtKeychain項目にのみ保存され、SQLiteにはプロバイダとクライアントIDだけが入ります。アクセストークンはメモリ内で有効期限前まで再利用します。更新トークンの差替えをOSストアへ保存できなければ認証を止めます。

同じプロバイダ・同じアカウント名のSMTP/IMAP4を新規認可する場合、一度のブラウザ認可結果を両方に使用します（要求scopeは両プロトコルを含む）。別のユーザーやプロバイダでは個別認可します。ブラウザの`Return to SmoothMail`はcallbackの受領を示し、接続テスト成功を意味しません。アプリ側でトークン取得・接続テストの進行と結果を確認してください。調査記録：[同意後の接続テスト](microsoft-oauth-callback-20261117.md)。

失効後はアカウント行の右クリックから対象プロトコルのOAuthを再認可できます。アカウントIDとメールキャッシュは維持します。背景更新の失敗を理由に勝手にブラウザを起動しません。暫定UIの説明は`spec/provisional/oauth-controls.md`を参照してください。

## 検証

**2026-11-14追記:** 通常GUIでのGoogle登録から再起動・更新までの確認待ち（AUTH-01）は、利用者本人の手動による正常動作確認報告で完了しました。[完了記録](manual-validation-20261114.md)を参照してください。以下は専用プローブ等の過去の検証経緯です。Microsoft実アカウント検証の完了を意味しません。

### 実アカウントの手動プローブ

`-DSMOOTHMAIL_BUILD_LIVE_GOOGLE_PROBE=ON`で、実装と同じ`OAuthClient`・`MailService`を使う`smoothmail_google_probe`を追加できます。既定OFFで、CTestでは自動実行しません。開発用ランタイムのQtプラグイン/SASL探索先を設定して、以下を実行します。

```bat
smoothmail_google_probe.exe --client-id <Desktop appのclient ID> --email <テスト用Gmailアドレス>
```

外部ブラウザで利用者が認可すると、コード交換→refresh→SSL/TLS接続のSMTP/IMAP XOAUTH2認証を順番に検証します。メール本文の読出し・送信・下書き追加は行いません。トークンはメモリ内のみで、資格情報ストア/DB/ファイルへ保存しません。プロトコルログ・TLS鍵ログを無効化し、ログには段階と成否だけを表示します。認可は最大5分、全体は最大7分です。通常のSafe Shell内でloopback/外部接続を拒否される場合、外側での実行には明示的な許可が必要です。

実メール操作も検証するときだけ、追加で`--self-mail-test`を指定します。このモードは**指定したGmailアカウント自身への日本語テストメール1通と下書き1件を作成**します。続いてINBOX/下書きフォルダを本体の`MailService::synchronize()`でメモリ内に読み込み、今回だけのUUID付き件名、UTF-8本文、送信元/宛先、Message-ID、UID/UIDVALIDITYを照合します。他のメールの内容も同期対象になりますが、内容や件名一覧はログやファイルに保存しません。送信/APPENDは自動再試行せず、読込みだけ最大6回まで再確認します。第三者への送信や削除は行わず、作成したテストメール・下書きはサーバーに残します。全体上限は10分です。実認証のみの許可とは分けて、メール操作・同期読込みの許可を得てから実行してください。

`--credential-restart-test`は別の明示opt-inモードです。実認可/refresh/認証後、refresh grantを`SmoothMail-live-google-<UUID>`という専用QtKeychainサービスへプロトコル別に2件保存し、同じEXEの新規子プロセスを起動します。子は本体の`AuthenticationBroker`でストアから読込み、実Googleでrefreshしてローテーションを保存し、IMAP/SMTPを再認証します。子終了後、親が両テスト資格情報を削除します。子の引数には公開ID・ファイルパスだけを渡し、tokenを引数/標準入力/ファイルへ渡しません。通常のSmoothMailサービス/既存資格情報には触れず、メール操作もしません。強制終了等で削除できなかった場合は表示されたテスト名前空間を使って手動削除し、必要ならGoogle側でアプリの認可を取り消してください。`--self-mail-test`との同時使用は拒否します。

Google Desktopアプリの登録JSONに`client_secret`が含まれる場合、トークン交換に必要になることがあります。ローカルの`SMOOTHMAIL_GOOGLE_CLIENT_SECRET`に設定して起動してください。チャットやGit管理ファイルには貼り付けません。`invalid_client`等は既知のエラーコードだけを表示し、Googleの生の応答文をログへ出しません。

手動プローブでは`--registration-file <ローカルJSONパス>`も使用できます。Google CloudからダウンロードしたDesktop app用JSONの`installed.client_id`が指定IDと一致することを確認し、`installed.client_secret`をメモリ内でだけ利用します。ファイル本文は出力・コピーしません。登録JSONはリポジトリ外へ保存してください。

2026-11-13の実検証では、テストユーザー追加後に認可応答を受け取りましたが、コード交換で`invalid_request`となりました。当時の手動プローブで、Googleが`client_secret is missing.`を返すことを確認済みです。このクライアントには登録時のclient_secretの設定が必要です。現在の同等プローブはC++の`smoothmail_google_network_probe --token-configuration CLIENT_ID`です（`SMOOTHMAIL_BUILD_LIVE_GOOGLE_PROBE=ON`で構築）。公開client IDと意図的に無効なダミーcode/verifierだけを送信し、本文や秘密情報は表示しません。明示的な手動検証専用で、ビルドやCTestからは実行しません。

その後、利用者指定のローカルDesktop登録JSONを用いた実試験では、Googleでの認可コード交換・refreshが成功しました。ただしメール認証開始直後にウィンドウなしプローブが終了し、SMTP/IMAPの結果は未確認です。この実行のexit code 0だけをメール認証成功とは扱いません。KJobのQEventLoopLocker解放による自動終了を防ぐ設定と、明示的な最終結果がない終了を失敗とする保護を追加しました。`--self-test-lifecycle`は認証情報・ネットワーク・ブラウザを使わない回帰テストで、CTest対象です。

**最新の再試験では実Google認証に成功しました。** 修正版プローブのログで`AUTHORIZATION_CODE_EXCHANGE_OK`、`REFRESH_OK`、`SMTP_XOAUTH2=OK`、`IMAP_XOAUTH2=OK`の全段階とexit code 0を確認しています（Safe Shell外・利用者の明示許可・ブラウザでの本人認可）。プローブはメール読出し・送信・下書き保存を行わず、tokenを永続化しません。そのため実メール操作や実Google tokenのOS保存・再起動復元の検証完了を意味しません。

**実メール操作の後続試験も成功しました。** 別の明示許可と本人認可の下で`--self-mail-test`を実行し、SMTP自己宛送信・IMAP下書きAPPEND・INBOX日本語読み戻し・下書き日本語読み戻しがすべてOK、exit code 0でした。今回のテストメール1通と下書き1件はサーバーに残しています。外部宛先やメール削除は試していません。

**実Google tokenのOS保存・別プロセス復元も成功しました。** さらに別の明示許可で`--credential-restart-test`を実行し、専用QtKeychain領域への2件の保存、新しい子プロセスの`AuthenticationBroker`によるIMAP/SMTP grant読込み・refresh・保存、両プロトコルの実XOAUTH2再認証を確認しました。最後は`KEYCHAIN_TEST_CLEANUP=OK`、`CREDENTIAL_RESTART_TEST=OK`、exit code 0で、テスト資格情報は削除済みです。これは本体の認証・保存処理を用いた専用プローブであり、通常GUIのアカウント追加/DB設定の保存/ウィンドウを伴う再起動操作を通した試験とは区別します。

### Googleで403 access_deniedになる場合

「テスト中で、デベロッパーに承認されたテスターのみがアクセスできます」と表示された場合、OAuthクライアントを作成した同じGoogle Cloudプロジェクトで、**Google Auth Platform → Audience（対象）→ Test users（テストユーザー）→ Add users**から、実際にログインするテスト用Googleアカウントを追加して保存します。旧UIでは「APIとサービス → OAuth同意画面 → テストユーザー」です。プロジェクト所有者のメールも、テストで使う場合は明示的に追加します。テストのために本番公開へ切り替えたり審査済みと扱ったりする必要はありません。

設定後は古い認可URLを使い回さず、新しくOAuth操作を開始してください。Testing状態のGmailスコープでは、認可・refresh tokenは通常7日で期限切れになる点にも注意します。

公式手順: https://developers.google.com/workspace/guides/configure-oauth-consent
対象ユーザーと有効期限: https://support.google.com/cloud/answer/15549945

### 合成環境の検証結果

Windows Sandboxの合成callback・差替えネットワーク応答・使い捨て資格情報ストアで7 passed（init/cleanup含む）。RFC 7636のPKCEベクトル、state検証、code/verifierの交換、refresh tokenのローテーションと保持、失効、HTTP redirect拒否、Microsoftの要求scope、取消を確認しています。保存上限超過でローテーション保存が失敗するとアクセストークンを返さないこと、OAuth項目がなくてもpassword項目へ切り替えないことも確認済みです。Google/Microsoftの実サービスで認可が成功したという意味ではありません。OSの証明書ストアと実アカウントを変更していません。

## 公式資料

- https://developers.google.com/identity/protocols/oauth2/native-app
- https://learn.microsoft.com/en-us/entra/identity-platform/scenario-desktop-app-configuration
- https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow
- https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth
