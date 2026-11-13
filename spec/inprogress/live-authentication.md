# 実サービスでの検証未完了事項

確認日: 2026-11-14

共通の対象仕様は、[OAuth対応先と認証・秘密情報保存](../tech.md)、[アカウント追加・編集](../ui.md)、[接続・更新動作](../behavior.md)。以下は**未実装ではなく実サービス検証の未完了**である。

AUTH-01（Googleの通常GUI）は、2026-11-14の利用者による手動確認報告で完了したため、この未完了管理から除外した。[完了記録](../../docs/dev/manual-validation-20261114.md)

## AUTH-02: Microsoft実アカウント

- **状態:** Google／Microsoft向けOAuth実装とMicrosoft要求内容の合成試験はあるが、Microsoftの実認可・認証・メール操作は未検証。
- **事情:** Microsoftのアプリ登録・実試験用設定／本人認可の準備が未提供で、利用者指定でも後回し。対象アカウント種別、redirect URI、委任権限、組織側のIMAP／SMTP AUTH許可が外部設定に依存する。
- **済んでいること:** 固定endpoint／scope、PKCE・state・callback検査、トークン更新・失効・保存失敗時の拒否、パスワードへの自動切替禁止。合成環境でMicrosoft向け要求scope等を検査。
- **完了条件:** public clientの登録と必要な許可を準備し、本人認可、コード交換、refresh、IMAP/SMTP XOAUTH2、必要な送受信／下書き、OS保存・再起動復元を確認する。組織側制限がある場合は拒否時の表示と安全な停止も確認する。
- **資料:** [Microsoft設定・検証手順](../../docs/dev/oauth.md)、[未検証範囲の記録](../../docs/dev/implementation-status.md)。

実試験をこの文書作成に伴って再開したり、秘密情報を収集したりはしていない。パスワード・トークン・登録JSON本文をチャットやGitへ残さず、必要な許可・本人操作が整ってから実施する。
