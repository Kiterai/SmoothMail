# Safe Shell内のビルド資材取得TLSエラー

2026-11-16。base `f6e4117`、専用worktree/branch `sandbox-tls-diagnosis`。
元worktree・並行ブランチ・既存依存・Qt SDK・通常起動runtimeは変更していない。
今回の実行はすべてSafe Shellのサンドボックス内。外側での実行はしていない。

## 結論と失敗の因果関係

1. Safe Shellはインストール済み`@anthropic-ai/sandbox-runtime` **0.0.81**を利用する。
2. Windows版runtimeは`srt-sandbox`アカウントでrunnerを起動し、その子を
   **write-restricted token**とjob objectで起動する。
   通常のUsersグループの書込み権限が指定外のディレクトリに及ぶのを防ぐ設計。
3. この実行トークンではWindows **SchannelのTLS資格情報初期化**が拒否される。
   ネットワーク接続なしの`AcquireCredentialsHandleW`でも
   `SEC_E_NO_CREDENTIALS` **0x8009030E**を再現する。
4. インストール済みCMake/libcurlはSchannelを使用するため、`file(DOWNLOAD)`は
   libcurlエラー35（SSL connect error）になる。従来入口もvcpkgのx-scriptも
   同じCMake取得へ集約していたので、両方が影響する。
5. Gitはruntimeが`http.sslBackend=openssl`を注入するため成功する。
   「Gitは成功するがCMakeは失敗する」は通信先の違いではなくTLS実装の違い。

`SEC_E_NO_CREDENTIALS`は今回、接続先がクライアント証明書を要求したという意味ではない。
サーバー証明書検証以前・HTTP通信以前のローカルSSPI初期化失敗である。
Windows内部のどの非公開分岐が拒否しているかまでは断定していない。

根拠となるインストール済みruntimeのREADME:

- `Security model`（768行付近）: 二段階起動と専用アカウント。
- `The child token and kept write grants`（795–821行付近）:
  write-restricted token、Schannel非対応、OpenSSLクライアントは動作すること、
  runtimeが常に`--write-restricted-token`を渡すことを明記。
- `dist/sandbox/windows-sandbox-utils.js`（2516行付近）にも、
  Schannelがwrite-restricted tokenで資格情報を取得できないためGitをOpenSSLへ
  切り替える実装コメントがある。

参照場所:
`C:/Users/User1/AppData/Roaming/npm/node_modules/@anthropic-ai/sandbox-runtime/`。
runtimeとSafe Shellは読み取りのみで、コード・設定・ACL・トークン生成は変更していない。

## 再現・除外した原因

| 検証 | 結果・解釈 |
|---|---|
| `IsTokenRestricted` | `True` |
| 接続なしのSchannel初期化 | 全4条件で0x8009030E |
| flags 0 / NO_DEFAULT_CREDS / MANUAL_CRED_VALIDATION併用 / STRONG_CRYPTO併用 | 同じ失敗。既定クライアント資格情報や証明書検証設定の変更では解消しない |
| `NCryptOpenStorageProvider` | 成功。暗号機能全体が利用不能という説明は不適切 |
| CMake 3.30.0、Qt付属3.30.5でpypi.org取得 | 両方status 35、LOG中にschannelとSEC_E_NO_CREDENTIALS |
| 同一サンドボックス・同一pypi.orgへのcurl | OpenSSL成功、System32 Schannel版はプロキシCONNECT 200後に35 |
| 固定GitHub vcpkg資材 | CMake失敗35、明示したOpenSSL curlでは成功、manifest SHA512一致 |

最低TLS版変更は以前の調査でも効果なし（`agent/dist03-clean-dependency-verification.md`）。
domain未許可によるHTTP拒否（CMake status 22）、書込み先のACL拒否、
外部／loopbackのWFP制限は別問題。CA追加・プロキシ認証変更・許可domain追加だけで
このSchannel初期化失敗は解消しない。

再現用:

```powershell
.\tests\manual\probe_schannel.ps1
cmake -P tests/manual/probe_cmake_tls.cmake
```

probeはログを分類するだけで、生のプロキシURL・認証情報・ヘッダを出力しない。
Schannel probeはネットワーク不要、OS証明書ストアや資格情報ストアを変更しない。

## 修正

`SMOOTHMAIL_DOWNLOAD_CURL=<OpenSSL対応curlの絶対パス>`を追加した。

- 未指定: 従来どおりCMake/libcurl。失敗時の自動切替なし。
- 指定: 子プロセスだけ`CURL_SSL_BACKEND=openssl`を設定し、`--version`でOpenSSLを確認。
  Schannel専用、存在しない実行ファイル、相対パスは拒否する。
- `.curlrc`を`--disable`で無効化。`--proto =https --proto-redir =https --tlsv1.2`、
  証明書検証、タイムアウト、最大redirect数を指定。
- サンドボックスのプロキシ環境はそのまま保持。取得後のSHA256/SHA512検査、
  排他、キャッシュ不一致拒否、一時ファイル清掃、非上書き公開は従来処理を共用。
- `BuildWindowsDependencies`から各ステージ、vcpkgのx-scriptまで同じ指定を渡す。
  `x-block-origin`も維持し、vcpkg組込み取得への暗黙fallbackを許さない。

以前の「外部curlへの切替は行わない」という方針を、隔離を維持するため明示選択式へ変更した。
トークン制限解除や通常環境実行、TLS検証無効化、プロキシ除去は採用していない。
使用方法は`docs/dev/build.md`。

## 検証結果

- `cmake -P tests/cmake/run_build_guards.cmake`: 成功。
  従来patch/receipt/staging/native profile/clean root検査と、
  新しい取得手段の拒否ケースを含むdownloadガード10件。
- `tests/cmake/run_download_tls.cmake`: 成功。
  CMake失敗35／OpenSSL新規取得成功／誤SHA512拒否／未信頼CA拒否60の4ケース。
  CA拒否ケースでは意図的に`insecure`を記した`.curlrc`も配置し、それが無視されることを確認。
  失敗時に完成ファイルや`.part`が残らないことも検査。
- `tests/manual/verify_sandbox_tools.cmake`: 終了0。
  新規root `C:/Users/User1/AppData/Local/Temp/opencode/tls-tools-20261116`で、
  固定NinjaソースをGit取得→MSVC 19.44で実ビルド、
  固定vcpkg-recipes取得、vcpkg.exe新規取得・SHA512検証、
  **vcpkg子プロセスからCMake 4.4.4／7zip 26.05／7zr 26.05の実取得・展開**まで成功。
  vcpkgの「Asset cache hit」は今回のx-script新規取得成功を意味し、既存rootの流用ではない。
  最初の試行はTemp書込み許可不足で停止し、許可後に新規rootで成功した。

証跡:

- `build/tls-tools.log`（Ninjaビルドとvcpkgの実取得、最終行で成功）。
- 最終TLS比較ログID `0fa1357cf76a4a6cb77ee378cb3b08e1`。
- 最終オフライン／Schannel probeログID `a43affdc946a4750a260ff12f161c2d6`。
- ツール検証実行ID `752e26224faf43d4987a45403b725d81`。
- TLS比較の成功資材は`build/download-tls-de4d57a5636b/openssl.exe`。

アプリ・全依存の約46分の再構築や製品ZIP検証を今回完走したという意味ではない。
対象のTLS失敗とその修正、実コンパイラ、vcpkgの多段取得経路をサンドボックス内で確認した。
検証環境のGit付属curlは7.83.0/OpenSSL 1.1.1oで、更新状況はこの修正の対象外。
継続利用にはセキュリティ更新されたcurl/OpenSSLを推奨する。別のOpenSSL trust bundleを
使うため、企業CA等が必要な環境では適切なPEM bundleを指定する。
