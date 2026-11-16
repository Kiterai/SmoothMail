# OAuthエラー文のプロバイダ分離

- `src/`のGoogle言及を調査。接続テスト経路の固定エラー文は`OAuthClient`の2種類。
- Googleの`invalid_request`かつ既知の`client_secret is missing.`に対する専用案内は、既に`m_provider == "google"`で限定されていた。
- `invalid_client`の案内はプロバイダ共通でGoogle Desktopのシークレットに言及し、Microsoftでも表示される問題があった。
- `invalid_client`を設定済みプロバイダで分岐。GoogleはDesktop appのID・登録シークレット、Microsoftはデスクトッププラットフォーム・`http://localhost`・`SMOOTHMAIL_MICROSOFT_CLIENT_ID`の一致を案内。Microsoft public clientにはシークレット不要と明記する。
- エラーコードは従来どおり保持し、生の`error_description`やトークンなどは表示しない。認証方式・送信パラメータは変更していない。
- 回帰テストは同一インスタンスでGoogle/Microsoftそれぞれの`invalid_client`、`unauthorized_client`、`invalid_request`、`invalid_scope`を処理。MicrosoftでGoogle案内が出ないこと、Google専用シークレット不足メッセージの分岐、未加工応答の非表示を確認。
- Debugビルド成功。差替えHTTPによる登録エラーテストは3 passed / 0 failed / 0 skipped（init/cleanup含む）、ログ`build/oauth-provider-error-results.txt`。ブラウザ・外部通信・localhost接続・資格情報保存なし。通常CTest 12ターゲットも成功（OS資格情報のopt-in試験は従来どおりskip）。
