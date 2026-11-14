# 配布に関する未完了事項

確認日: 2026-11-14

## DIST-01: Release配布物

- **対象仕様:** [配布形態](../distribution.md)、[Qt SDKとビルド環境](../tech.md)。
- **状態:** パッケージ生成処理は実装済み。実Release ZIPの生成・クリーン環境検証は未完了。
- **事情:** 新仕様に合わせた依存の内部取得・構築とCMake/C++の配布経路を実装したが、配布用legalディレクトリと実同梱物全体の監査は未完了。依存の手動収集は今後の前提にしない。Release CRTはMSVC開発環境から探索する。
- **済んでいること:** `cmake/PackageWindows.cmake`による依存取得・Release構築→必要ファイル収集→検査→ZIP化の入口。旧Python補助・ガードはCMake/C++へ移植し、合成ZIPを独立したCMake/libarchiveで展開・SHA256照合。内部Ninja構築、統合Debug/Release依存構築、両構成のアプリビルド、Release DLL収集とホスト19 CTest対象成功。既存SDKと通常起動用runtimeは変更していない。実Releaseランタイムの公開はDIST-04により停止。
- **未完了の範囲:** 実際のRelease依存でコマンド一行の生成を完走すること、ZIPだけをクリーンWindowsへ持ち込み、展開したEXEから直接起動してDLL／プラグイン探索・通信を確認すること。Debug版や試験用ZIPを製品配布物とは扱わない。
- **完了条件:** 指定Qt SDKとABI・構成を合わせたRelease依存、変更済み依存のreceipt、再配布可能なCRT、DIST-02の通知類を準備。生成コマンド・成果物ハッシュ・検査結果・クリーン環境での直接起動／通信結果を記録する。
- **資料:** [配布手順と現在の不足](../../docs/dev/distribution.md)、[依存構築](../../docs/dev/dependencies.md)、[生成処理](../../cmake/PackageWindows.cmake)。

## DIST-02: 配布物の監査

- **対象仕様:** [配布形態の共通注意事項](../distribution.md)、[採用技術・ライセンス方針](../tech.md)。
- **状態:** プライバシー・通知対応付けの自動検査は実装済み。実配布物全体のライセンス監査と最終レビューは未完了。
- **事情:** 同梱対象が確定したRelease DLL／プラグインごとに、上流ライセンス原文・著作権・変更差分・対応ソース提供方法・差替え可能性等を点検する必要がある。限定的なKIOパッチや開発用通知だけでは、推移依存全体の監査完了にはならない。
- **済んでいること:** `THIRD_PARTY_NOTICES.md`と依存ごとのパッチ・変更通知・構築検証。Debug混入、未解決DLL、通知不足、絶対パス等を検出して生成を止めるガード。
- **未完了の範囲:** 実同梱物に対応する`legal/manifest.json`と通知一式の準備、LGPL等の条件・CRT再配布権限の確認、不要なGPL系worker／ビルド専用ツール等の非混入確認。バイナリやZIPに個人情報・PC内情報がないことの最終レビュー。
- **完了条件:** 全同梱ファイルの構成表とライセンス対応表を作成し、通知・対応ソース・改変／再構築手順を整備。自動検査と人による成果物レビューの結果を記録する。検査通過だけで法的条件の充足や個人情報の完全非混入を断定しない。
- **資料:** [第三者通知](../../THIRD_PARTY_NOTICES.md)、[legal形式・プライバシー検査](../../docs/dev/distribution.md)。

## DIST-03: キャッシュのない環境での取得検証

- **状態:** 固定NinjaソースのGit HTTPS取得・MSVC/NMake構築に成功。CMake downloaderのオフラインガードも成功。ただし全資材の新規HTTPS取得完走は未確認。
- **事情:** この環境のCMakeからGitHub releaseへ接続するとTLS接続エラー（status 35）。既存のvcpkg/SASL/資材キャッシュを利用する構築成功とは区別する。
- **完了条件:** 利用するプロキシ/CAを正しく設定したクリーン環境で、vcpkg本体・SASL・GNU/MSYS2等の資材取得とハッシュ検証を確認する。証明書検証を無効化したり、未検証キャッシュを配布して迂回しない。

## DIST-04: 上流依存の絶対インストールパス

- **状態:** KConfigの絶対パスは承認済み限定パッチで対処し、Debug/Release構築・無効化回帰・export維持・KConfig DLLの検査通過を確認。実Releaseランタイム全体は他のDLLの検出により停止しており、ZIPは公開していない。
- **原因:** 上流KConfig 6.31.0の`src/core/CMakeLists.txt`が`KCONF_UPDATE_INSTALL_LOCATION`へ絶対libexecパスを定義し、`KConfig::checkUpdate()`が利用する。これは`__FILE__`ではないためコンパイラのパスマッピングでは除去できない。
- **済んでいること:** `patches/kconfig/6.31.0-optional-check-update.patch`を固定原本の独立コピーへ適用し、Windowsで外部設定移行を無効化。経緯・機能制限・ライセンス説明は`patches/kconfig/README.md`、要約は`THIRD_PARTY_NOTICES.md`へ保存。receiptを生成・検証し、不足と置換の拒否も合成試験済み。既定ONの別ビルドも成功。
- **残る検出の調査:** 39ファイル中15 DLLが拒否。KCrash/KIOCoreは生成ヘッダのhelper/plugin絶対位置、GNU依存2件は実ビルド先のPDBとgettextのlocale/lib/元prefix、Qt10件はSDKに元々ある絶対PDB参照（Widgetsには.cppパスも）と判明。Qt10件はSDKと同じバイト列で双方のAuthenticodeがValid。SASLとQtCoreの固定既定値は個人パスとしての過剰検出も含む。
- **GNUの対処:** `cmake/native-triplets/x64-windows.cmake`のリンカ設定とgettext-libintlの論理prefixで、ソースパッチや検査除外なしに2件を解消。旧prefixを残し`native/gnu-runtime-v1`へ別構築。カタログの既定パスは二重スラッシュ/UNCやMSYS実インストール先にならないよう検証する。移動先を明示してbindする合成UTF-8/複数形カタログ・文字コード変換も試験する。詳細は`agent/gnu-native-privacy-settings.md`。
- **GNU最終確認:** 最終rootでbootstrap内の強制試験、KI18n/KServiceとアプリのDebug/Release構築、ホスト19 CTest対象が成功。`build/gnu-runtime-final`のGNU DLLは最終prefixとSHA256一致し、GNU検出0件・全体の拒否13件を確認。旧prefixは非変更。
- **未完了の範囲:** 各原因への安全な対処と全体検査。KIOのpluginパスはadmin workerの信頼判定にも使うため単純削除しない。固定既定値・供給元署名SDKのメタデータ・今回の作業者情報を区別する配布/検査方針の明確化が必要。検査は変更せず、Qtを事後書換えして署名を壊す対処も採用していない。詳細は`agent/release-runtime-audit-investigation.md`。
- **設定範囲の調査:** 上流の通常設定によるfull変数/マクロの上書きは効かず、相対libexec指定も絶対prefixが付加されることをconfigure実験で確認。静的KConfigと`/Gy`・`/OPT:REF`で未使用`checkUpdate()`を除去した最小EXEは検査通過したが、同処理を参照する対照EXEは拒否された。静的案は全依存の状態共有・リンク構成・再リンク/ライセンス条件の再検討が必要で、未採用。
- **完了条件:** ビルド者のパスを埋め込まず、実行ファイル探索の安全性と必要機能を維持して構築し、実ランタイム全体の検査を通す。バイナリ文字列の書換えや検査除外で回避しない。
