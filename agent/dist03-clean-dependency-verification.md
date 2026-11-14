# DIST-03: キャッシュなしの依存取得検証

2026-11-14。base `8f08c2b`、専用worktree/branch `verify/dist03-clean-fetch`。
並行作業中の元worktree・既存依存prefix・キャッシュ・通常起動runtime・Qt SDKは変更しない。

## TLS失敗の切り分け

- CMake 3.30.0、同じ公式GitHub HTTPS URLで比較。
- Safe Shell未許可domainではstatus 22。domain許可後はstatus 35を再現。
- CMakeのLOGはメモリ内だけで分類し、プロキシURL/認証情報/ヘッダや環境変数の値は出力しない。
- TLS既定値・最低1.2・最低1.3すべてで、Schannel `AcquireCredentialsHandle`の`SEC_E_NO_CREDENTIALS`を確認。
- 利用者承認の通常環境では同じ`DownloadAsset.cmake`の新規取得が成功し、固定SHA512も一致。
- 証明書検証ON・最低TLS 1.2は変更していない。CAやTLS検証の問題と断定せず、サンドボックス実行主体のSchannel制約として切り分けた。
- 検査用probeは`build/probe-clean-download.cmake`、`build/probe-tls.cmake`。

## 再現用の検証入口

`cmake/VerifyWindowsCleanDependencies.cmake`を追加。

- MSVC x64開発環境と既存Qtを使い、未存在の短いDEPENDENCY_ROOTだけを受理。
- 全Gitソースを固定commitで新規取得し、NinjaをMSVC/NMakeで内部構築。
- vcpkgのdownload/native/tools rootを新領域へ固定。バイナリキャッシュ・外部overlayは使用しない。
- 標準BuildWindowsDependenciesを使い、実際の全Release依存構築で遅延取得されるツール/MSYS資材も検証。
- Git・CMake/libcurlの公式HTTPS経路と固定commit/資材ハッシュ照合を維持。
- 成功時だけ`clean-verification-completed.json`を保存。既存rootの上書き・削除や、別rootからの資材コピーをしない。
- 作業者準備対象のQt SDK/MSVC/Windows SDK/CMake/Git/Pythonの新規導入、アプリ/ZIP公開・legal監査・クリーンPC検証とは区別する。

## オフライン回帰

`tests/cmake/run_build_guards.cmake`へ、root未指定・既存ディレクトリ・既存ファイル・Qt未指定の4拒否ケースを追加。
既存内容が不変で、無効入力で新しいrootを作らないことを確認した。
従来のdownload/patch/receipt/staging/native profileガードと合わせて成功。
ログ: `build/dist03-offline-guards.log`。

## 実取得・構築

通常環境での全取得・構築の実行結果を確認してから、DIST-03の完了可否を確定する。
