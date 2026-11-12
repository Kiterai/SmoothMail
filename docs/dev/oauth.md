# Google / Microsoft OAuth

Qt Network上にAuthorization Code + PKCE S256 + state検証を実装しています。ブラウザはシステムの外部ブラウザを使用し、callbackはIPv4 loopbackのランダムポートに限定します。暗号化・乱数・SHA-256はQtの機能を利用します。

- Google: `https://accounts.google.com/o/oauth2/v2/auth`、`https://oauth2.googleapis.com/token`。スコープは`https://mail.google.com/`。callbackは`http://127.0.0.1:<port>/`。
- Microsoft: `https://login.microsoftonline.com/common/oauth2/v2.0/authorize` / `token`。スコープは`https://outlook.office.com/IMAP.AccessAsUser.All`、`https://outlook.office.com/SMTP.Send`、`offline_access`。callbackは`http://localhost:<port>/`。

エンドポイントは上記に固定し、HTTPSの証明書検証を無効にしません。トークンPOSTのredirectも追跡しません。state/codeの重複、不一致、Hostの不一致、不正callbackを拒否し、有効な認可コードだけを1回交換します。トークン応答サイズ・タイムアウトを制限し、失効は再認可が必要なエラーとして報告します。パスワード認証への自動切替はありません。

## 外部で必要な設定

GoogleではDesktop app、MicrosoftではMobile and desktop applicationsのpublic clientとしてアプリを登録してください。Microsoftのsystem browser redirect URIは`http://localhost`です。登録した対象アカウント種別と委任権限、組織側のIMAP/SMTP AUTH許可を確認してください。Googleのメールスコープは公開形態によって検証・審査が必要です。

現在、利用するアプリ登録情報と実サービスのテスト認可は未提供です。秘密情報をチャットへ貼り付ける必要はありません。

## 検証

Windows Sandboxの合成callbackと差替えネットワーク応答で6 passed（init/cleanup含む）。RFC 7636のPKCEベクトル、state検証、code/verifierの交換、refresh tokenのローテーションと保持、失効、HTTP redirect拒否、Microsoftの要求scope、取消を確認しています。Google/Microsoftの実サービスで認可が成功したという意味ではありません。OSの証明書ストアと実アカウントを変更していません。

## 公式資料

- https://developers.google.com/identity/protocols/oauth2/native-app
- https://learn.microsoft.com/en-us/entra/identity-platform/scenario-desktop-app-configuration
- https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow
- https://learn.microsoft.com/en-us/exchange/client-developer/legacy-protocols/how-to-authenticate-an-imap-pop-smtp-application-by-using-oauth
