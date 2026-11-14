# 配布に関する未完了事項

確認日: 2026-11-14

## DIST-01: Release配布物

- **対象仕様:** [配布形態](../distribution.md)、[Qt SDKとビルド環境](../tech.md)。
- **事情:** 新仕様に合わせた依存の内部取得・構築とCMake/C++の配布経路を実装したが、配布用legalディレクトリと実同梱物全体の監査は未完了。依存の手動収集は今後の前提にしない。Release CRTはMSVC開発環境から探索する。
- **完了範囲:** `cmake/PackageWindows.cmake`による依存取得・Release構築→必要ファイル収集→検査→ZIP化の入口。旧Python補助・ガードはCMake/C++へ移植し、合成ZIPを独立したCMake/libarchiveで展開・SHA256照合。内部Ninja構築、統合Debug/Release依存構築、両構成のアプリビルド、Release DLL収集を確認。GNU対策後のホスト19 CTest対象、KCrash/KIO統合後の20対象が成功。既存SDKと通常起動用runtimeは変更していない。資料: [配布手順と現在の不足](../../docs/dev/distribution.md)、[依存構築](../../docs/dev/dependencies.md)、[生成処理](../../cmake/PackageWindows.cmake)。
- **未完了範囲:** 実Release ZIPの生成・公開。DIST-02のlegal整備とDIST-04の検査側対応を含め、実際のRelease依存でコマンド一行の生成を完走すること、ZIPだけをクリーンWindowsへ持ち込み、展開したEXEから直接起動してDLL／プラグイン探索・通信を確認すること。Debug版や試験用ZIPを製品配布物とは扱わない。
- **完了条件:** 指定Qt SDKとABI・構成を合わせたRelease依存、変更済み依存のreceipt、再配布可能なCRT、DIST-02の通知類を準備。生成コマンド・成果物ハッシュ・検査結果・クリーン環境での直接起動／通信結果を記録する。

## DIST-02: 配布物の監査

- **対象仕様:** [配布形態の共通注意事項](../distribution.md)、[採用技術・ライセンス方針](../tech.md)。
- **事情:** 同梱対象が確定したRelease DLL／プラグインごとに、上流ライセンス原文・著作権・変更差分・対応ソース提供方法・差替え可能性等を点検する必要がある。限定的なKIOパッチや開発用通知だけでは、推移依存全体の監査完了にはならない。
- **完了範囲:** 依存ごとのパッチ・変更通知・構築検証。Debug混入、未解決DLL、通知不足、絶対パス等を検出して生成を止めるガード。統合収集物の作業者情報検査はDIST-04を参照。資料: [第三者通知](../../THIRD_PARTY_NOTICES.md)、[legal形式・プライバシー検査](../../docs/dev/distribution.md)。
- **未完了範囲:** 実同梱物に対応する`legal/manifest.json`と通知一式の準備、LGPL等の条件・CRT再配布権限の確認、不要なGPL系worker／ビルド専用ツール等の非混入確認。バイナリやZIPに個人情報・PC内情報がないことの最終レビュー。
- **完了条件:** 全同梱ファイルの構成表とライセンス対応表を作成し、通知・対応ソース・改変／再構築手順を整備。自動検査と人による成果物レビューの結果を記録する。検査通過だけで法的条件の充足や個人情報の完全非混入を断定しない。

## DIST-03: キャッシュのない環境での取得検証

- **対象仕様:** [配布形態](../distribution.md)、[Qt SDKとビルド環境](../tech.md)。
- **事情:** この環境のCMakeからGitHub releaseへ接続するとTLS接続エラー（status 35）。既存のvcpkg/SASL/資材キャッシュを利用する構築成功とは区別する。
- **完了範囲:** 固定NinjaソースのGit HTTPS取得・MSVC/NMake構築に成功。CMake downloaderのオフラインガードも成功。
- **未完了範囲:** キャッシュのない環境で、全資材の新規HTTPS取得とハッシュ検証を完走すること。
- **完了条件:** 利用するプロキシ/CAを正しく設定したクリーン環境で、vcpkg本体・SASL・GNU/MSYS2等の資材取得とハッシュ検証を確認する。証明書検証を無効化したり、未検証キャッシュを配布して迂回しない。

## DIST-04: 上流依存の絶対インストールパス

- **対象仕様:** [配布形態・プライバシー](../distribution.md)、[採用技術・ライセンス方針](../tech.md)。
- **事情:** 上流依存の生成ヘッダやPDB参照にはビルド先が埋め込まれる。KConfigの`KCONF_UPDATE_INSTALL_LOCATION`などは`__FILE__`ではなく、コンパイラのパスマッピングだけでは除去できない。自前構築分は対処したが、現行の自動配布検査は利用者が保持を許容したQt/SASLの外部由来パスも拒否する。固定既定値・供給元情報・今回の作業者情報の区別が必要。
- **完了範囲:**
  - KConfig: 固定原本の独立コピーへ[限定パッチ](../../patches/kconfig/README.md)を適用し、Windowsの外部設定移行を無効化。Debug/Release構築・無効化回帰・export維持・DLL検査に成功。receiptの不足・置換拒否と既定ONの対照構築も確認。変更通知は[第三者通知](../../THIRD_PARTY_NOTICES.md)へ保存。
  - 原因調査: KConfig対処後の39ファイル中15 DLLの拒否を、KCrash/KIOCoreのhelper/plugin絶対位置、GNU2件のPDB/locale/lib/prefix、Qt10件の供給元PDB/一部ソース参照、SASL/QtCoreの固定既定値に分類。[調査記録](../../agent/release-runtime-audit-investigation.md)。
  - GNU: tripletのリンカ設定とgettextの論理prefixで対処し、旧prefixを残して`native/gnu-runtime-v1`へ別構築。UNC/MSYSパス化の防止、文字コード変換、明示bindしたUTF-8/複数形カタログの再配置、KI18n/KServiceとアプリ両構成の構築、ホスト19 CTest対象が成功。`build/gnu-runtime-final`のGNU DLLは最終prefixとSHA256一致・検出0件、全体の拒否は13件。[検証記録](../../agent/gnu-native-privacy-settings.md)。
  - KCrash/KIO: 独立worktreeでビルド先フォールバックを除去し、Windowsのadmin workerは明示拒否。信頼判定を空文字化せず、favicon除外を維持。独立prefixで両構成・Releaseアプリ・ホスト18 CTest対象・Debug追加試験が成功。両DLLの検査は通過し、当時の旧GNU/SASL/Qt由来の拒否は13件。[検証記録](../../agent/kde-runtime-path-patches.md)。
  - 統合: GNU対策`f69b83c`とKCrash/KIO対策`5d29d85`を統合。nativeプロフィール・gettextホストツール分離・パッチハッシュ別KIOビルド先を保持し、KCrash/KIOの両構成、Releaseアプリ・試験EXEを構築。ホスト20 CTest対象（`native_runtime_relocated`・`portablekde`を含む）が成功。実行から`oauthclient|connectiontester|mailtransport`を除外。
  - 統合後の全体検査: `764a613`から新規収集した39ファイルで今回の作業者のパス・ユーザー名・PC名の検出0件。残る11ファイルはQt10（SDKとSHA256一致・署名Valid）とSASL1（上流固定値）のみ。[検査記録](../../agent/integrated-runtime-builder-info-audit.md)。
  - 設定範囲の調査: 通常設定によるfull変数/マクロの上書きは効かず、相対libexec指定にも絶対prefixが付加されることを確認。静的KConfigと`/Gy`・`/OPT:REF`の最小EXEは未使用`checkUpdate()`の除去で検査通過したが、参照する対照EXEは拒否。状態共有・リンク構成・再リンク/ライセンス条件の再検討が必要なため静的案は未採用。
- **未完了範囲:** 許容されたQt/SASLの外部由来パスを、今回の作業者情報と区別する自動配布検査の実装と検証。検査規則・Qt SDKは未変更で、実ZIPは公開していない。legal・実ZIPのレビュー・クリーン環境検証はDIST-01/02に残る。
- **完了条件:** ビルド者のパスを埋め込まず、実行ファイル探索の安全性と必要機能を維持して構築する。許容する情報の由来を確認したうえで実ランタイム全体の検査を通す。バイナリ文字列の書換えや、DLL名による一括除外で回避しない。
