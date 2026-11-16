# 技術仕様整理後の実装引継ぎ

`af34abb`で簡素化した`spec/tech.md`の旧記述（同コミットの`spec/tech.md.bk`）から、保守に必要な情報を整理した。正式な採用技術は[tech.md](../spec/tech.md)、ライセンス方針は[license-handling.md](../spec/license-handling.md)を正とする。この資料は採用技術の追加承認や、整理前の仕様の復元を意味しない。

## ビルドとSDKの整合性

- 独自コードはC++20、Windowsは指定Qt SDKに合わせたMSVC 2022 x64を基本とする。構築入口はCMakeであり、Ninja・vcpkgは内部で準備する。旧記述のツール列挙を手動導入要件と解釈しない。
- 既存Qt SDKを`Qt6_ROOT`または`CMAKE_PREFIX_PATH`で指定する。共通設定に個人環境の絶対パスを固定せず、別のQtを自動導入しない。CraftによるQt込みの一括導入は標準手順とせず、依存管理でも指定済みの外部Qtを維持する。
- KIMAP/KSMTP/KMimeと推移依存は、同じQt・コンパイラ・アーキテクチャ・ランタイム構成で構築し、別Qtにリンクしたバイナリを混在させない。
- 旧記録のQt 6.12.0/MSVC 2022は検証環境の例であり、必須の個人パスではない。Debugに加えてRelease構築も後続で成功している。macOS/Linuxの互換性確認を意味しない。
- 固定タグ・commit・最小Qt版は[依存manifest](../cmake/dependencies.json)と[依存構築資料](docs/dependencies.md)で確認する。KMimeのFrameworks移管後の版とKIMAP/KSMTP側の要求を別々に確認する。
- 現在の実行手順は[ビルド手順](../docs/dev/build.md)。内部ツールの導入経緯は[ビルド体制移行](build-tool-migration.md)を参照する。

## 実装構成と安全上の注意

- Qt Core/Gui/Widgets/Network/Sqlを使用し、テストにはQt Testを使う。ローカルDBはQt SQLのQSQLITEドライバで扱う。
- 通信・MIME解析・DBアクセスでGUIスレッドをブロックしない。DB接続は専用ワーカー内で作成・使用・破棄する。実装経緯は[実装引継ぎ](development.md)、大規模処理の制約は[未完了事項](../spec/inprogress/implementation-limits.md)を参照する。
- メール本文はQTextBrowser/QTextDocumentへプレインテキストとして挿入し、URLの範囲だけをリンク化・装飾する。メール由来HTMLをそのままHTML表示APIへ渡さない。HTMLメールは当面ソース表示とし、MIME解析結果の値モデルと表示処理を分離する。
- OS連携にはQStandardPaths、QDesktopServices、QSystemTrayIconを使用し、OS固有パスを直書きしない。
- パスワード・refresh tokenはQtKeychainで保存する。平文への自動フォールバックは禁止する。保存失敗や長いtokenの扱いは[依存構築資料](docs/dependencies.md)と[OAuth資料](docs/oauth.md)を参照する。

## OAuthの構成

- 現実装の対象はGoogle/Microsoft。他プロバイダへの拡張は未定。
- Qt Network上の認証連携層で、外部ブラウザ・Authorization Code・PKCE S256・state検証・loopback callbackを使用する。暗号アルゴリズムやTLSを自作せず、乱数とSHA-256にはQtの機能を使う。
- Qt NetworkAuthはGPL/商用ライセンスのため、非GPL配布を維持する現構成では使用しない。Gmail API/Microsoft Graphによるメール操作SDKも使用しない。
- access tokenはKIMAP/KSMTPのXOAUTH2認証へ渡す。パスワード認証への暗黙の切替を行わず、サーバが必要な認証機構を広告しない場合はtokenを送らず失敗させる。
- 各社へのアプリ登録が必要。Googleのメールスコープは`https://mail.google.com/`で、公開形態に応じた検証・審査要件を確認する。Microsoftは`https://outlook.office.com/IMAP.AccessAsUser.All`、`https://outlook.office.com/SMTP.Send`、`offline_access`を使用し、組織側のSMTP AUTH制限も扱う。
- redirect URI、更新・失効・保存失敗時の動作、外部設定と検証根拠は[OAuth資料](docs/oauth.md)に集約済み。未承認の操作方針は[OAuth暫定UI](../spec/provisional/oauth-controls.md)を参照する。

## 変更済み依存の保守

旧tech.mdにあったパッチの詳細は[依存構築資料](docs/dependencies.md)と各パッチREADMEに残っているため、正式な採用技術一覧へ戻さない。

- KIO 6.31.0のfaviconキャッシュのGPL系実装は、承認済み限定パッチで除外する。[KIO](../patches/kio/README.md)に差分・機能制限・残る義務を保存する。
- WindowsのKCrash/KIOはビルド時prefixへのhelper探索フォールバックを除き、実行時配置に従う。KIO admin workerは信頼パス判定の単純削除ではなく明示拒否とする。公開API・KIOのTLSエラー情報・favicon除外を維持するが、元prefixにしかないhelperとadmin利用は保証しない。[KCrash](../patches/kcrash/README.md)・[KIO](../patches/kio/README.md)を参照する。
- WindowsのKConfig 6.31.0は未使用の`checkUpdate()`を無効化する。公開APIは残すが、呼出しは警告のみで設定移行や外部プロセス実行を行わない。[KConfig](../patches/kconfig/README.md)に理由・制限・ライセンス条件・撤去条件を保存する。
- 原本・既存Qt SDKを変更せず、独立したパッチ作業コピーを使う。変更済み依存のreceiptを検証し、[第三者通知](../THIRD_PARTY_NOTICES.md)と差分を保持する。機能除外だけで改変LGPLライブラリの対応ソース提供等の義務がなくなるわけではない。

## 検証情報の移管先と履歴上の注意

| 旧記述の範囲 | 管理先・判断 |
|---|---|
| MITと第三者ライセンス、告知・対応ソース・差替え可能性、GPL-only混入防止 | [ライセンス方針](../spec/license-handling.md)、[配布手順](../docs/dev/distribution.md)。監査経緯は[監査資料](docs/distribution-legal-audit.md) |
| Qt未導入WindowsでのTLS・Cyrus SASL・XOAUTH2配布／探索／認証、長いtokenと保存失敗 | [依存構築・合成試験記録](docs/dependencies.md)、[OAuth検証記録](docs/oauth.md) |
| 日本語MIME・添付名・フォルダ名、UIDVALIDITY/UID・既読／削除同期、30秒更新時の応答性 | [GUI検証](docs/gui-checks.md)、[設定・移動](docs/settings-and-moves.md)、[大規模メール](docs/large-mailboxes.md)、[手動確認](docs/manual-validation-20261114.md) |
| Google実認可・refresh・XOAUTH2、自己宛送受信・下書き読戻し、専用OSストア保存と別プロセス復元 | [OAuth検証記録](docs/oauth.md)。通常GUIでの登録・再起動・更新は[利用者の手動確認](docs/manual-validation-20261114.md)と区別する |
| Microsoft実アカウント | 利用者の正常ログイン報告と明示指示によりAUTH-02をクローズ。[OAuth資料](docs/oauth.md)に確認範囲を記載。合成試験やログイン成功を未実施の個別試験の証拠にしない |
| 将来のHTML表示 | [未承認の設計案](../spec/provisional/html-rendering.md)。macOS/Linuxと同様、将来方針を現行仕様の未完了項目に数えない |

バックアップには「Releaseおよび他OSの互換性は未検証」と、後段の「Debug/Release構築確認済み」が混在していた。また「Release配布と全ライセンス監査は未完了」は後続履歴で解消済みだった。

- `44366d2`：利用者の最終確認に基づき配布物監査を完了。[最終構成・確認記録](license-display-final-zips.md)を参照。
- `28a4a68`：標準配布入口の通し実行と独立ZIP検査を完了。[検証記録](dist01-standard-entry-verification.md)を参照。
- `2157200`：完了済み配布項目を未完了管理から削除。これらをバックアップから復活させない。
- `ef345fc`：その後の一覧UI・新ZIP構成の実装検証は[別記録](list-layout-and-package-updates.md)。過去の製品ZIP確認を、変更後の全成果物の再検証済みという意味へ拡張しない。

現状の残件は[未完了一覧](../spec/inprogress/README.md)を正とする。各記録時点の検証、合成試験、実サービス試験、製品配布物の確認を混同せず、公開・必要ファイルの正当な配布は現行仕様に従って配布者が行う。
