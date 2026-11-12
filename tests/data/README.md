# テスト専用TLS証明書

`tls-cert.pem`と`tls-key.pem`はSmoothMailのローカル合成サーバー専用に生成した自己署名証明書・公開済みダミー鍵です。製品には含めず、本番や個人の秘密情報には使用しません。

生成方法（OpenSSL）:

```sh
openssl req -x509 -newkey rsa:2048 -nodes -keyout tls-key.pem -out tls-cert.pem -days 3650 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

TLS成功テストだけがプロセス内のQt既定設定へこの証明書をCAとして追加し、終了後に設定を復元します。OSの証明書ストアは変更しません。未信頼証明書の拒否テストではCAを追加しません。
