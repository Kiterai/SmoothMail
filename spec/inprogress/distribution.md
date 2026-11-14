# 配布に関する未完了事項

確認日: 2026-11-14

## DIST-01: Release配布物

- **対象仕様:** [配布形態](../distribution.md)、[Qt SDKとビルド環境](../tech.md)。
- **事情:** 新仕様に合わせた依存の内部取得・構築とCMake/C++の配布経路を実装したが、配布用legalディレクトリと実同梱物全体の監査は未完了。依存の手動収集は今後の前提にしない。Release CRTはMSVC開発環境から探索する。
- **完了範囲:** `cmake/PackageWindows.cmake`による依存取得・Release構築→必要ファイル収集→検査→ZIP化の入口。旧Python補助・ガードはCMake/C++へ移植し、合成ZIPを独立したCMake/libarchiveで展開・SHA256照合。内部Ninja構築、統合Debug/Release依存構築、両構成のアプリビルド、Release DLL収集を確認。GNU対策後のホスト19 CTest対象、KCrash/KIO統合後の20対象が成功。既存SDKと通常起動用runtimeは変更していない。資料: [配布手順と現在の不足](../../docs/dev/distribution.md)、[依存構築](../../docs/dev/dependencies.md)、[生成処理](../../cmake/PackageWindows.cmake)。
- **未完了範囲:** 実Release ZIPの生成・公開。DIST-02のlegal整備を含め、実際のRelease依存でコマンド一行の生成を完走すること、ZIPだけをクリーンWindowsへ持ち込み、展開したEXEから直接起動してDLL／プラグイン探索・通信を確認すること。DIST-04の検査側対応は完了済み。Debug版や試験用ZIPを製品配布物とは扱わない。
- **完了条件:** 指定Qt SDKとABI・構成を合わせたRelease依存、変更済み依存のreceipt、再配布可能なCRT、DIST-02の通知類を準備。生成コマンド・成果物ハッシュ・検査結果・クリーン環境での直接起動／通信結果を記録する。

## DIST-02: 配布物の監査

- **対象仕様:** [配布形態の共通注意事項](../distribution.md)、[採用技術・ライセンス方針](../tech.md)。
- **事情:** 同梱対象が確定したRelease DLL／プラグインごとに、上流ライセンス原文・著作権・変更差分・対応ソース提供方法・差替え可能性等を点検する必要がある。限定的なKIOパッチや開発用通知だけでは、推移依存全体の監査完了にはならない。
- **完了範囲:** 依存ごとのパッチ・変更通知・構築検証。Debug混入、未解決DLL、通知不足等のガードと[DIST-04のパス検査](../../docs/dev/distribution-path-policy.md)。統合Release候補39ファイルを19コンポーネントに一意に対応付け、SHA256一覧・取得済みのライセンス原文/著作権・固定ソース情報・パッチ説明を`build/legal-windows-review`へ収集。[事前監査資料と確認事項](../../docs/dev/distribution-legal-audit.md)、`cmake/PrepareWindowsLegalReview.cmake`を作成。レビュー用の`manifest.review.json`は配布入力として拒否され、単に改名してもQt/CRT原文不足で拒否されることを確認。候補にはPDB・worker・ビルド専用ツールを含まない。
- **未完了範囲:** Qtの適用ライセンス・対応する原文/内蔵第三者コード通知/完全ソース、配布者のMSVC CRT再配布権限、対応ソースの具体的な提供方法の確認。コンパイル/リンク対象・ヘッダ/生成物に対するコード別条件の最終監査、配布用`legal/manifest.json`の確定、差替え・実ZIPの最終レビュー。原本LICENSES集合の収集だけで採用コード全体の許諾判定を完了したとは扱わない。
- **完了条件:** 全同梱ファイルの構成表とライセンス対応表を作成し、通知・対応ソース・改変／再構築手順を整備。自動検査と人による成果物レビューの結果を記録する。検査通過だけで法的条件の充足や個人情報の完全非混入を断定しない。

## DIST-03: キャッシュのない環境での取得検証

- **対象仕様:** [配布形態](../distribution.md)、[Qt SDKとビルド環境](../tech.md)。
- **事情:** この環境のCMakeからGitHub releaseへ接続するとTLS接続エラー（status 35）。既存のvcpkg/SASL/資材キャッシュを利用する構築成功とは区別する。
- **完了範囲:** 固定NinjaソースのGit HTTPS取得・MSVC/NMake構築に成功。CMake downloaderのオフラインガードも成功。
- **未完了範囲:** キャッシュのない環境で、全資材の新規HTTPS取得とハッシュ検証を完走すること。
- **完了条件:** 利用するプロキシ/CAを正しく設定したクリーン環境で、vcpkg本体・SASL・GNU/MSYS2等の資材取得とハッシュ検証を確認する。証明書検証を無効化したり、未検証キャッシュを配布して迂回しない。

## 完了した項目

DIST-04（上流依存の絶対インストールパス）は、ビルド側対処と供給元情報の限定的な由来判定を実装し、統合Release候補39ファイルの検査・合成回帰に成功したため未完了管理から除外。[方針・完了記録](../../docs/dev/distribution-path-policy.md)、[検証証跡](../../agent/dist04-path-policy-validation.md)。legal付き実ZIP・クリーン環境検証はDIST-01/02に残す。
