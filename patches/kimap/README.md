# KIMAP 26.08.3のSASL認証修正

- 上流: https://invent.kde.org/pim/kimap
- 固定コミット: `fe0df283d6745c5ad1e0478db0a74ea0a2205d5a`（v26.08.3、ライブラリ版6.8.3）
- パッチ: `26.08.3-fix-sasl-continuation.patch`
- 変更日・変更者: 2026-11-13、SmoothMail contributors

上流の`src/loginjob.cpp`（LGPL-2.0-or-later）の著作権・SPDX表示は保持します。SmoothMail独自コードのMITとは別の条件です。上流未採用のローカル修正です。

## 修正範囲

1. PLAIN以外でも空のSASLチャレンジをSASL実装へ渡す。SASL-IRがないサーバーからの空の継続応答は、XOAUTH2の初期応答を促す正当な応答です。認証方式やTLS設定は変更しません。
2. 不正な応答では`errorText`だけでなく`UserDefinedError`を設定し、認証中のSASL接続を解放する。ログイン成功と誤判定されることを防ぎます。

## 適用と配布

`cmake/BuildMailDependencies.cmake`で固定ソースの独立コピーへ適用します。原本・Qt SDKは変更しません。`cmake/PreparePatchedSource.cmake`がHEAD・index・差分・追加ファイル・逆適用可能性を検証し、予期しない変更があるコピーは拒否します。ビルドは従来の未修正KIMAPとは別のディレクトリを使います。

prefixの`share/licenses/kimap`へ、上流の`LICENSES`、パッチ、変更通知を配置します。配布に必要な対応ソース・ビルド手順・ライブラリ差替え可能性等の整備は別途必要です。`THIRD_PARTY_NOTICES.md`も参照してください。

## 検証

指定Qt 6.12.0／MSVCでDebug構築し、外部通信を無効にしたWindows Sandboxで`mailtransport`を実行しました。未修正DLLは12 passed／3 failed、修正DLLは15 passed／0 failed（init／cleanup含む）。XOAUTH2の空チャレンジ、通常の認証拒否、不正応答の非0エラーを確認しています。PLAIN／LOGIN、SMTPの3機構、STARTTLS非対応時の認証禁止も成功しています。実サービスのOAuth認可、TLSハンドシェイク、SASL-IR経路はこの検証の対象外です。
