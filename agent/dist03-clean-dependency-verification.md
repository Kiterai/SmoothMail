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
- Safe Shell診断log ID: `c931b0179f044caa8fa97b7ca2267750`。通常環境の成功log ID: `9babda665a854f159e13ce253fdeadf8`。

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

**DIST-03は完了。** 利用者承認の通常環境で`build/run-dist03-clean.cmd`を1回実行し、
2026-11-14 22:56:09〜23:42:09（JST、約46分）、終了コード0で全Release依存構築が完走した。
作業rootはOS Temp配下の専用`opencode/dist03-clean-deps`。開始時に存在しないことを入口で確認して作成した。
失敗したprobeの資材も含め、他rootからのダウンロード・ビルド資材コピーは行っていない。

- Gitソース17件の実HEADとmanifestの固定commitが一致。構築後も全17件のtracked変更なし。
- 取得物一覧52ファイルのSHA512を完了receiptと再照合してすべて一致。
- vcpkg.exeはnative manifestの固定SHA512、Cyrus SASL原本は固定SHA256とも再照合して一致。
- vcpkgのCMake x-scriptによる資材新規取得の成功は49件。
  vcpkgの「Asset cache hit」はこのx-script成功を意味し、開始前の既存キャッシュ利用ではない。
  同じ検証runの後半で同じ資材を再使用することとは区別する。
- vcpkgのホストツールrootは約27分、製品GNU rootは約10分で完了。
  libiconv/gettext-libintlは両構成を構築し、Release bin監査・GNU再配置の強制試験も成功。
- Native、Framework、WinFlexBison、I18n、Solid、KIO、SASL、XOAuth2、QtKeychain、Mailの全標準ステージが成功。
  KIMAP/KSMTPまでinstall済み。今回の検証はRelease依存の構築であり、アプリ両構成や実サービス試験を再実施したという意味ではない。
- 途中で親ログが静かだった期間も、gettext-libintlの内部config.logが約15秒で261KB→518KBへ増加していることを確認。
  ハングではなく、configureが詳細を内部ログへ書いていた。

証跡:

- `build/dist03-clean-build.log`
- 専用root内`clean-verification-started.txt`
- 専用root内`clean-verification-completed.json`
  - SHA256: `d48f59a771c0e3cd2f8334400441e5d01d7b1b45e7635411793f8e68c1fb215a`
  - ソースmanifest SHA256: `67b06d64bb92a93779ea2a66070655e0478e51d2e93a40703d27b1c097814ae3`
  - native manifest SHA256: `94656fc1af81fc16b8259e3a08effba5b6dc722be8e544d9cf35eeae57614551`
- Safe Shell実行ID: `106bbdb7bd9d45bd8f6816633c33c4d1`
- 構築後の独立照合log ID: `fc87a7dc369d425fb80b3e93ccdcc681`、`727ac23acb204b2892584983c8339fed`

Qt SDK・MSVC/Windows SDK・CMake/Git/Pythonは指定された既存環境を使用した。
空のWindowsへのSDK/ツール導入、Qt対応ソースの配布監査、legal付き実ZIP・クリーン環境の起動確認はDIST-01/02側に残る。
証明書検証を無効化せず、外部curl/Pythonダウンローダーや未検証キャッシュへ切り替えていない。
通常起動runtimeや元worktree、他ブランチの未コミット変更には触れていない。自動merge/pushは行わない。

## 元worktreeへの統合

2026-11-15、利用者の依頼により元worktreeの`master`（`01271fd`）へ専用ブランチをno-ffマージ。
DIST-02の並行更新を保持し、競合なく統合できた。未コミットの`AGENT.md`・`spec/spec.md`と未追跡`spec/docs.md`は変更・ステージしない。
統合後にオフラインのpatch/receipt/staging/download/native profileと新規clean rootの入力ガードが成功。
ログ: `build/dist03-merge-guards.log`。約46分の新規取得・構築は専用rootの成功記録を使用し、マージ時には重複実行していない。
元のQt SDK・通常起動runtime・他worktreeは変更せず、ブランチ削除やpushも行っていない。
