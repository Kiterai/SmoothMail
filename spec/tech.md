# 採用技術/依存ライブラリ/依存API

## 確定方針

- アプリ本体の独自コードはMITライセンスを想定する。第三者ライブラリはそれぞれのライセンスに従い、配布物全体をMITとして扱わない。
- GUI基盤はQt6。
- メール処理はKIMAP（IMAP）、KSMTP（SMTP）、KMime（MIME解析・生成）を使用する。
- OAuthの対応先はGoogleおよびMicrosoftとする。他プロバイダへの拡張は未定。
- Windowsを優先し、将来のmacOS/Linux対応を視野に入れる。
- HTMLメールは当面ソース表示とし、将来のレンダリングに備えてメール解析結果と表示処理を分離する。

## Qt SDKとビルド環境

- Qt6は開発者が別途インストールしたSDKを使用する。ビルドシステムが別のQtを自動導入する構成にはしない。
- 開発者は使用するQt SDKのパスをCMakeのQt6_ROOTまたはCMAKE_PREFIX_PATHで指定する。共通設定に個人環境の絶対パスを固定しない。
- 現開発環境ではC:/Qt/6.12.0/msvc2022_64が存在することを確認済み。このSDKを利用する方針とする。ただし、KDE依存を含むビルド互換性は未検証。
- KIMAP、KSMTP、KMimeおよび必要な依存は、指定SDKと同じQt・コンパイラ・アーキテクチャ・ランタイム構成でビルドする。別のQtにリンク済みのバイナリを混在させない。
- CraftによるQtを含む一括導入は標準の開発手順としない。依存管理ツールを使用する場合も、指定した外部Qt SDKを利用できることを条件とする。

## 選定する実装構成

- C++20、CMake、Ninja。Windowsは指定SDKに合わせてMSVC 2022 x64を基本とする。
- Qt Core/Gui/Widgets/Network/Sqlを使用する。テストにはQt Testを使用する。
- ローカルDBはQt SQLのQSQLITEドライバで扱う。
- 通信・MIME解析・DBアクセスはGUIスレッドをブロックしない。DB接続は専用ワーカーで作成・使用・破棄する。
- 本文はQTextBrowser/QTextDocumentにプレインテキストとして挿入し、URLの範囲だけリンク化・装飾する。メール由来のHTMLをそのままHTML表示APIに渡さない。
- OS連携にはQStandardPaths、QDesktopServices、QSystemTrayIconを使用し、OS固有パスの直書きを避ける。
- パスワード・リフレッシュトークンの保存にはQtKeychainを使用する。平文への自動フォールバックは禁止する。

## OAuth

- Qt Network上にGoogle/Microsoft専用の認証連携層を設ける。外部ブラウザ、Authorization Code、PKCE S256、state検証、loopback callbackを使用する。
- 暗号アルゴリズム・TLSは自作しない。PKCEの乱数・SHA-256にはQtの機能を使用する。
- Qt NetworkAuthはGPL/商用ライセンスのため、非GPL配布を維持する現構成では使用しない。
- 各社へのアプリ登録が必要。GoogleのIMAP/SMTP用スコープはhttps://mail.google.com/とし、公開形態に応じた検証・審査要件を確認する。
- Microsoftはhttps://outlook.office.com/IMAP.AccessAsUser.All、https://outlook.office.com/SMTP.Sendおよびoffline_accessを使用する。組織側でSMTP AUTHが制限される場合を扱う。
- アクセストークンをKIMAP/KSMTPのXOAUTH2認証へ渡す。パスワード認証への暗黙の切替は行わない。Gmail API/Microsoft Graphによるメール操作SDKは使用しない。

## 将来のHTML表示

- 現時点ではQt WebEngine/WebViewをビルド・実行時依存に含めない。
- 将来の第一候補はQt WebEngineとするが、正式採用は別途判断する。
- 解析結果の値モデルとレンダラーを分離し、認証情報や通信セッションを表示エンジンへ渡さない。
- HTML表示導入時には、ライセンス、外部通信遮断、能動機能制限、サンドボックス、ローカルファイルアクセス禁止、継続的なセキュリティ更新を検証する。

## 採用版固定前の検証事項

- 指定Qt SDKでのKIMAP/KSMTP/KMimeおよび推移依存のビルド。KMimeのFrameworks移管を踏まえ、互換タグと最小Qt版を確認する。
- 採用タグ・配布DLL・プラグインのライセンス監査。LGPLの告知、対応ソース提供、差替え可能性等を満たし、意図しないGPL-only依存を含めない。
- Qt未導入のWindows環境でのTLS、Cyrus SASLおよびXOAUTH2機構の配布・探索・認証。
- Google/Microsoftそれぞれのredirect URI登録、認証、更新、失効、秘密情報保存。長いトークンの保存と保存失敗も確認する。
- 日本語MIME・添付名・フォルダ名、IMAPのUIDVALIDITY/UIDと既読・削除同期、30秒更新時のUI応答性。
- Qtのみを用いたメイン画面基盤は、指定SDKとMSVCでビルド・自動テストを確認済み。KDE依存を含むビルド・実通信・クリーン環境への配布は未検証。ライブラリ採用と動作検証完了は区別する。
