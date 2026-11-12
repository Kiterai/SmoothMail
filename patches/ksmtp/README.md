# KSMTP 26.08.3のEHLO能力情報リセット

- 上流: https://invent.kde.org/pim/ksmtp
- 固定コミット: `f78c9bbcf690f5f829ccaf31921caec650579f20`（v26.08.3、ライブラリ版6.8.3）
- 変更: `26.08.3-reset-ehlo-capabilities.patch`
- 変更日・変更者: 2026-11-13、SmoothMail contributors

`src/session.cpp`（LGPL-2.1-or-later）の元の著作権・ライセンス表示を保持します。上流未採用のローカル変更です。

STARTTLS後にはTLS前のSMTP能力情報を破棄する必要があります（RFC 3207 §4.2）。上流はAUTH方式を追加する際に既存リストを消さないため、TLS後に広告されなくなった方式も`availableAuthModes()`へ残ります。SmoothMailの認証前チェックを正しく行うため、握手開始時にAUTH/SIZE/STARTTLS/DSN/SMTPUTF8の広告情報をリセットします。TLSの検証や認証方式の代用は変更しません。

`cmake/BuildMailDependencies.cmake`で原本とは独立した作業コピーへ適用し、別ビルドディレクトリで構築します。prefixの`share/licenses/ksmtp`へ上流LICENSES・差分・変更通知を配置します。全依存の配布監査完了を意味しません。

参照: https://www.rfc-editor.org/rfc/rfc3207#section-4.2

Windows Sandboxで、TLS前はXOAUTH2、TLS後はPLAINだけを広告する合成サーバーを使用して比較しました。未修正版はAUTHを送信して回帰テストが失敗（2 passed/1 failed）、修正版は認証コマンド・秘密情報を送信せず拒否して成功（3 passed/0 failed、init/cleanup含む）。
