# 配布に関する未完了事項

確認日: 2026-11-14

## DIST-01: Release配布物

- **対象仕様:** [配布形態](../distribution.md)、[Qt SDKとビルド環境](../tech.md)。
- **事情:** 新仕様に合わせた依存の内部取得・構築とCMake/C++の配布経路を実装したが、配布用legalディレクトリと実同梱物全体の監査は未完了。依存の手動収集は今後の前提にしない。Release CRTはMSVC開発環境から探索する。
- **完了範囲:** `cmake/PackageWindows.cmake`による依存取得・Release構築→必要ファイル収集→検査→ZIP化の入口。旧Python補助・ガードはCMake/C++へ移植し、合成ZIPを独立したCMake/libarchiveで展開・SHA256照合。内部Ninja構築、統合Debug/Release依存構築、両構成のアプリビルド、Release DLL収集を確認。GNU対策後のホスト19 CTest対象、KCrash/KIO統合後の20対象が成功。既存SDKと通常起動用runtimeは変更していない。資料: [配布手順と現在の不足](../../docs/dev/distribution.md)、[依存構築](../../agent/docs/dependencies.md)、[生成処理](../../cmake/PackageWindows.cmake)。
- **未完了範囲:** 実Release ZIPの生成・公開。DIST-02のlegal整備を含め、実際のRelease依存でコマンド一行の生成を完走すること、ZIPだけをクリーンWindowsへ持ち込み、展開したEXEから直接起動してDLL／プラグイン探索・通信を確認すること。DIST-04の検査側対応は完了済み。Debug版や試験用ZIPを製品配布物とは扱わない。
- **完了条件:** 指定Qt SDKとABI・構成を合わせたRelease依存、変更済み依存のreceipt、再配布可能なCRT、DIST-02の通知類を準備。生成コマンド・成果物ハッシュ・検査結果・クリーン環境での直接起動／通信結果を記録する。

## DIST-02: 配布物の監査

- **対象仕様:** [配布形態の共通注意事項](../distribution.md)、[採用技術・ライセンス方針](../tech.md)。
- **事情:** 同梱対象が確定したRelease DLL／プラグインごとに、上流ライセンス原文・著作権・変更差分・対応ソース提供方法・差替え可能性等を点検する必要がある。限定的なKIOパッチや開発用通知だけでは、推移依存全体の監査完了にはならない。
- **完了範囲:** 依存ごとのパッチ・変更通知・構築検証と[DIST-04のパス検査](../../agent/docs/distribution-path-policy.md)。統合候補39ファイル/19コンポーネントの原文・著作権・固定ソース・変更説明を収集。仕様`71ea2a1`のQtオープンソース利用・同ページでのソース別配布・情報ダイアログへの通知集約を反映。固定qtbase/qtsvgを取得し、原文/第三者通知候補を含む309ファイルを`build/legal-windows-complete-evidence-review`へ収集。モジュールはLGPL-3.0-only、FreeTypeはFTLを選び謝辞を追加。同梱Qt10起点のSDK依存63パッケージを列挙し参照未解決0件。ソース21アーカイブの作成・独立展開/SHA256照合・KDE有効7パッチの適用確認に成功。情報ダイアログのオフライン表示と版情報を実装し、Debugホスト22対象・独立Releaseレビュー12対象・配布補助4対象が成功。review_only資料は改名しても配布入力として拒否。[監査資料](../../agent/docs/distribution-legal-audit.md)、[ソース提供・表示手順](../../docs/dev/distribution.md)。
- **未完了範囲:** Qt等の実コンパイル/リンク対象・ヘッダ/生成物・内蔵第三者コードと条件/通知の最終照合、配布者のMSVC CRT利用資格・契約遵守の確認。提供ソースからの再構築・ライブラリ差替え・最終バイナリとの対応確認、実公開先でのソース取得確認、配布用`legal/manifest.json`の確定、実ZIPの最終レビュー。原本LICENSES集合の収集やSDK依存列挙だけで採用コード全体の許諾判定を完了したとは扱わない。
- **完了条件:** 全同梱ファイルの構成表とライセンス対応表を作成し、通知・対応ソース・改変／再構築手順を整備。自動検査と人による成果物レビューの結果を記録する。検査通過だけで法的条件の充足や個人情報の完全非混入を断定しない。

## 完了した項目

DIST-03（キャッシュのない環境での取得検証）は、2026-11-14に未存在rootから固定Gitソース17件とvcpkg/GNU/MSYS2/SASL等を新規取得し、全Release依存構築・構築後のcommit/ハッシュ照合に成功したため未完了管理から除外。TLS証明書検証ONを維持し、status 35はSafe Shell内のSchannel制約と切り分けた。[完了記録・再実行手順](../../agent/docs/clean-dependency-verification.md)、[検証証跡](../../agent/dist03-clean-dependency-verification.md)。

DIST-04（上流依存の絶対インストールパス）は、ビルド側対処と供給元情報の限定的な由来判定を実装し、統合Release候補39ファイルの検査・合成回帰に成功したため未完了管理から除外。[方針・完了記録](../../agent/docs/distribution-path-policy.md)、[検証証跡](../../agent/dist04-path-policy-validation.md)。legal付き実ZIP・クリーン環境検証はDIST-01/02に残す。
