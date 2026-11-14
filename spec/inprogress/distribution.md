# 配布に関する未完了事項

確認日: 2026-11-15

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
  - 2026-11-15、CRTを必要なimportの再帰的選択へ変更し、5 DLL（全体34ファイル）へ縮小。署名・x64・版・原本一致、全体のパス検査、配布補助6対象に成功。ネットワーク無効Sandboxで検証ZIPの展開/全ハッシュ・GUI起動・5 CRTの展開先からの実ロードを確認。MSVCの正当利用を配布作業担当者の責務とする方針を手順へ明記。[検証記録](../../agent/msvc-crt-app-local-policy.md)。legal付き最終ZIPの検証完了とは扱わない。
  - 同日、利用者から実組込みコードと条件の照合・最終通知と適用条件は「おおむね問題ないことを確認」との報告。技術検証では展開ソースからQt/KDE/SASL/GNUとSmoothMailを独立再構築し、同一EXEへの再構築DLL・改変Qt SVG/KCodecsの差替え、検証候補34ファイル×2構成の由来対応を確認。初回Sandbox各245ケース成功・1失敗・0スキップ。共通のCRT assertion試験不備は後続修正し、局所回帰はRelease 9件／Debug 10件成功。[診断修正](../../agent/diagnostics-abort-regression.md)、[初回再構築・差替え記録](../../agent/dist02-source-rebuild-replacement.md)。
  - 提供済みアーカイブのSHA256／tree pinと安全なパッチ作業コピーによる標準構築経路を追加。Solid／WinFlexBisonを含む23アーカイブの収集、source commit引渡し、任意の独立Qt構築に対応。合成12ケースと配布補助7対象が成功。コミットした新提供物からの全再試験は実施前のため未完了を維持。[実装記録](../../agent/source-archive-build.md)。
- **未完了範囲:** 利用者の概ね確認済みとの報告を最終配布構成の通知・条件・対応表へ反映して確定すること。提供済み依存アーカイブをsrcへ展開して既存ビルド入口で直接使う試験は`.git`必須のため失敗し、構築用Solid/WinFlexBisonアーカイブも別途補充した。固定Gitソースをネットワーク再取得する通常経路の失敗を示すものではないが、提供物を直接使う標準経路の再構築成功とは扱わない。完全な提供物からの再試験、SDKと再構築Qtの設定差の整理、最終配布バイナリとのソース対応確定、実公開先でのソース取得確認、配布用`legal/manifest.json`の確定、実ZIPの最終レビューが残る。MSVCの正当利用は配布作業担当者の責務として方針確定済みで、追加の資格承認待ちではない。原文収集・SBOM・検証用候補の対応記録だけで、最終製品の許諾や完全な対応ソース提供を認定しない。
- **完了条件:** 全同梱ファイルの構成表とライセンス対応表を作成し、通知・対応ソース・改変／再構築手順を整備。自動検査と人による成果物レビューの結果を記録する。検査通過だけで法的条件の充足や個人情報の完全非混入を断定しない。

## 完了した項目

DIST-03（キャッシュのない環境での取得検証）は、2026-11-14に未存在rootから固定Gitソース17件とvcpkg/GNU/MSYS2/SASL等を新規取得し、全Release依存構築・構築後のcommit/ハッシュ照合に成功したため未完了管理から除外。TLS証明書検証ONを維持し、status 35はSafe Shell内のSchannel制約と切り分けた。[完了記録・再実行手順](../../agent/docs/clean-dependency-verification.md)、[検証証跡](../../agent/dist03-clean-dependency-verification.md)。

DIST-04（上流依存の絶対インストールパス）は、ビルド側対処と供給元情報の限定的な由来判定を実装し、統合Release候補39ファイルの検査・合成回帰に成功したため未完了管理から除外。[方針・完了記録](../../agent/docs/distribution-path-policy.md)、[検証証跡](../../agent/dist04-path-policy-validation.md)。legal付き実ZIP・クリーン環境検証はDIST-01/02に残す。
