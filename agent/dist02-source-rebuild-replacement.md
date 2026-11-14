# DIST-02: 対応ソースの再構築・DLL差替え・バイナリ対応検証

2026-11-15。**技術検証の記録であり、最終配布物／対応ソースの承認ではない。**

利用者から、実組込みコードと条件の照合、および最終通知・適用条件について「おおむね問題ないことを確認」との報告を受けた。今回の依頼は残る再構築・差替え・ソース／バイナリ対応の検証。人の確認報告を、エージェントによる法的認定や最終manifestの承認へ置き換えない。

後続追記: 共通の診断失敗はReleaseで消える`_ASSERTE`を原因とする試験不備と判明し、実CRT abortの記録と構成別試験へ修正。局所回帰はRelease 9件／Debug 10件成功。[修正記録](diagnostics-abort-regression.md)。以下の初回検証結果は変更せず履歴として保持する。

## 結論

- 提供済みの依存アーカイブを`DEPENDENCY_ROOT/src`へ展開し、既存入口で直接使う試験は**失敗**。依存ビルド処理が`.git`を要求する一方、ソースアーカイブには意図どおり含まれていない。新規rootへ固定Gitソースをネットワーク再取得する通常経路の失敗を示すものではなく、その取得試験はDIST-03の別記録がある。
- 独立した診断用の直接構築では、SmoothMail、同梱するQt Base/SVG・KDE・QtKeychain・SASL/XOAuth2・GNUの構築に成功。CRTは仕様どおりMicrosoft原本を未改変で使い、再構築対象としない。
- ただしSolid／WinFlexBisonは現21アーカイブに不足しており、固定版を別途補った。gettextホストツール、Ninja、vcpkg実行ツール等も既存の構築用ツールを使用。完全な空環境／配布ソースだけの再構築成功とは扱わない。
- 同じEXEのまま、Qt/GNU再構築DLLと、明示的に改変したQt SVG／KCodecs DLLへの差替えに成功。新規オフラインSandboxで両構成とも19テストEXEを実行し、各245ケース成功・1失敗・0スキップ。
- 共通の失敗は`DiagnosticsTest::fatalExceptions(crt-assert)`の子プロセス終了待ち15秒。差替え後だけの失敗ではないが、全試験成功とも扱わない。
- 新しい検証候補について、19コンポーネント／34ファイル×2構成のソース資料・構築出力・配置物の対応を68レコードで確認。既存の保存済みRelease候補や、今後の最終製品ZIPの対応確認まで完了したとは扱わない。

## 入力と構築

- SmoothMailソース: `45654e19fd8ed5df346bb8feb1186998ef6a152a`。
- `cmake/PrepareWindowsSourceReview.cmake`で新規`build/dist02-source-rebuild-review`へ21アーカイブを収集。`tests/cmake/verify_source_review.cmake`でハッシュ・独立展開・有効7 KDEパッチの適用可能性を再確認。
- 作業先は`build/d02`。展開ソースは`src`、独立したパッチ適用コピーは`patched`、依存の構築・インストールは`build`／`install/Release`。
- MSVC 2022、compiler 19.44.35229.0、x64、Release。Qt Base/SVGは固定ソースから`qtbase-build`／`qtsvg-build`へ構築し、`qt-install`へインストール。既存Qt SDKのインストール先を使わない。
- Qtはshared Release、テスト／examples／SBOM生成OFF、Schannel ON、OpenSSL OFF、SQLiteは内蔵版。既存SDKと設定が完全一致する構築ではない。比較した公開設定ヘッダではqconfigにdebug-and-release／OpenSSL関連5行、qtnetwork-configにOpenSSL関連4行の差があり、Core/Gui/Widgets/Sql/Xmlの各モジュール設定ヘッダに差はなかった。
- KDEの有効パッチと構築オプションは展開したSmoothMailの`cmake`／`patches`を使用。ただしGit必須チェックとGit cloneによるパッチ作業コピー生成だけは、アーカイブのハッシュ確認済みソースを直接渡す診断用ラッパーに置換。標準経路の修復やガード無効化を製品へ導入したわけではない。
- 補充した構築用ソース: Solid `e0164c17b4d94c11aaf02e9b37ab9d23e660b51e`、WinFlexBison `1176da2adaf972b2216b1ac8dd611e128fe80bc5`。固定checkoutのコミットから別途archiveを生成した。
- GNUは配布予定のlibiconv 1.19／gettext 0.22.5原本と、展開したvcpkg-recipes・SmoothMail tripletを使用。新規install root、binary cache無効、既存ライブラリパッケージの流用なしで再構築。ダウンロード済みの工具／原本アーカイブは新しいdownloadsへコピーし、port/toolのハッシュ検証を維持。ネットワーク取得は遮断した。
- GNU DLLはアプリ／KDE構築後に別途再構築し、最終差替え構成で利用した。KDE構築時のGNU／gettextホストツールは既存のもの。GNU全体を先に再構築してから全依存を再構築する完全な連鎖試験ではない。
- SmoothMailは展開ソースと新規KDE/SASL prefixから`app-build`へ構築。`SMOOTHMAIL_SOURCE_COMMIT`へ上記commitを明示し、legalはレビュー用資料を使用、`SMOOTHMAIL_DISTRIBUTION=OFF`。配布承認済みEXEにはしない。

## 差替え試験

1. 新しく構築したEXE・KDE/SASL系DLLと既存SDK/GNUからbaselineを収集。
2. EXE・テストEXEを変更せず、Qt Base DLL・qwindows/qoffscreen/qsqlite/qschannelbackend・GNU DLLを新規構築版へ交換。
3. Qt SVGとKCodecsは、追加export関数が45654を返す改変版へ交換。既存関数やABIを変えず、`replacement-marker.cpp`と`target_sources`の1行だけを追加した。
4. `tests/source-replacement/verify-modified-source.ps1`で、元の全ファイルと追加2箇所を照合。Qt SVGは`src/qtsvg`、KCodecsは有効パッチ適用後の`patched/kcodecs`を比較元とする。
5. `source_replacement_probe`でUnicode・XML・SVG描画・テキスト・SQLite・RFC2047復号とローカルDLL由来を確認。差替え版では両exportの実呼出しを要求。baselineへ同じ要求をすると欠落として失敗する負例も確認。
6. 新規・ネットワーク無効SandboxでZIP全ハッシュ、通常GUI起動、ロード元、19テストEXE、GNUの変換／カタログ機能を両構成で検査。GUIは`nativeControllerWorkflow`と`editsFoldersSendsMailAndDeletesAccountDuringFetch`の2ワークフローを実行した。GUI全ケースや実サービス試験ではない。

結果は各構成18 EXE成功・診断1 EXE失敗（QtTestケース合計245成功・1失敗・0スキップ）。通信41ケース、IMAP/SMTP認証15ケース、OAuth 8ケース、資格情報6ケース、GUI 4ケース（init/cleanup込み）が成功。実メール・実アカウント・ホスト資格情報は使用せず、guestで合成データと使い捨てのダミー資格情報だけを使用した。

ホストCTestは17/20成功。OAuth／接続／mailtransportが失敗／timeoutとなり、詳細再実行ではループバックTCP接続不可を確認。同じ構築物のSandbox通信試験は成功したため、ホスト失敗を再構築バイナリの正常性の根拠にも、成功件数にも含めない。

初期試験ではPowerShell `Start-Process`の終了コード取得が不安定だったため、guest runnerをnative process handleを保持する方式へ修正。最終試験は修正後のrunnerで実施。意図的なCRT assertion試験の失敗は修正／除外せず、両構成の結果をfalseで保持した。

## ソース／バイナリ対応と証跡

`build/d02/source-binary-review.json`は、ソースアーカイブSHA256・有効パッチ資料・構築出力SHA256・配置ファイルSHA256・改変内容を記録する。新規構築出力と配置物の一致を検査し、baselineのSDK QtCoreに限り既存windeployqtによるprefix再配置を別操作として記録した。これは製品のパス検査を免除する設定ではない。

Qt SDKバイナリを再現したという主張はしない。SDKとのソース識別子の対応は供給元SBOMに基づき、再構築版は前述の異なる設定で検証したもの。バイト一致するreproducible buildの証明、SDKと同一構成の全オプション・生成物の照合、最終製品EXEとの対応確定は残る。

| 証跡 | SHA256 |
|---|---|
| 検証EXE（両構成で同一） | `22c5eb8a2503fe23fd652420d9e596928217d622864afa6e8bcb4c8058f2e46f` |
| `build/d02/source-binary-review.json` | `3dd2000525f8c4e249fdc2e9a8509e955af9b070679d3537cde5e6f003a24b72` |
| `build/d02/test-modifications-review-final.json` | `3dd4d4a927157237e165ee9f8c82ab96b995eb544ca282434ef9f10d046afd9e` |
| `build/dist02-sandbox-output/result.json` | `239758acc53ca9e59f6835bf94bd5af0114defcb8cab8badf399218b73c790fc` |
| baseline検証専用ZIP | `b1bee7dc62e4d9d173ce5546c39349c4539c5512bedc5218bbe28a63f731c061` |
| replacement検証専用ZIP | `095774eaa8f1c1ea02b7e7d8e7ee5e02ce6a4ba3a82ca53ae3c05b6d7e5f964c` |

主要ログ: `build/dist02-{source-collect,source-extract,export-build-first,deps-direct,deps-direct-resume,qt-build,modified-build,gnu-build-retry,app-build,app-tests,attribution-final,modified-inputs-final,gnu-runtime-final}.log`。直接構築用の実行ラッパーは`build/dist02-*.cmake`／`build/dist02-attribution.ps1`。guestの各テストログと画面は`build/dist02-sandbox-output/{baseline,replacement}`へ回収済み。初期試験の失敗も`build/dist02-sandbox-output-initial`へ保持した。

両Sandboxは出力回収後に終了済み。既存Qt・通常起動先・保存済み候補は更新していない。検証候補には最終通知・プライバシー監査を適用しておらず、ZIPも製品として公開／配布しない。

## 残件

1. Git管理情報なしの提供済み依存アーカイブを安全に検証・パッチ適用・構築できる入口と、完全な構築用ソース／ツール案内を整備する。ネットワーク再取得が必要な通常経路と区別し、今回の診断ラッパーだけで解消扱いにしない。`BuildWindows.cmake`からのexported source commit引渡しも点検する。
2. Solid／WinFlexBisonと、必要な構築設定／生成入力をソース提供範囲へ反映し、最終提供物から再試験する。
3. CRT assertion試験の終了待ちとRelease診断動作の切り分けは後続修正で完了（[修正記録](diagnostics-abort-regression.md)）。全体再試験は別途行う。
4. 最終配布構成・通知を確定してEXE／ソースを再構築・再収集し、その組合せのハッシュ・差替え・プライバシー・公開先取得を検証する。最終`legal/manifest.json`は今回生成していない。
