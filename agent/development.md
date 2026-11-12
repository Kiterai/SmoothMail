# 実装の引継ぎ

- 独自コードはC++20。既存Qt SDKをQt6_ROOT/CMAKE_PREFIX_PATHで指定し、別Qtを取得しない。
- 最初の実装はアプリ起動・空のメイン画面・トレイ常駐。通信等の未実装操作は無効化し、テスト用メールを通常画面へ混入させない。
- トレイを利用できない環境ではcloseを受理して操作不能な常駐を避ける。このフォールバックは暫定仕様を参照。
- アイコンはmainwindow.cppのQPainter描画による独自の封筒図形。外部素材なし。
- Qtテストはoffscreenプラットフォームで実行。実デスクトップでのトレイ動作は別途確認が必要。
- 次の大きな課題は、指定Qt SDKに対するKIMAP/KSMTP/KMimeとCyrus SASL/XOAUTH2のビルド・ライセンス・配布検証。未完了。

## 検証記録

- Qt 6.12.0 msvc2022_64、MSVC 19.44でDebugビルドとmainwindowのQt Test（4ケース）が成功。
- サンドボックス内でcmdシェルを使用し、vcvars64.batをcallしてからQt付属のCMake/Ninjaを使用。Git Bashからcmd /cを渡すと引数変換でコマンドが実行されない場合があるため避ける。
- CTest実行時はQt SDKのbinをPATHへ追加する。GUIテストはoffscreenであり、実トレイ操作の保証にはならない。
- 本文ビューを含むDebugビルドとCTestの全2ターゲットが成功。ACL付与エラーはユーザによる追加readルールの削除後に解消。
- テストの詳細ログはQt Testの`-o build/dev/name-results.txt,txt`で取得できる。Safe Shellのstdoutが空でもログを確認可能。mainwindowは6 passed、messageviewerは23 passed（init/cleanupを含む）。offscreenでフォント警告があるためピクセル外観の保証はしない。
- `cmake/BuildMimeDependencies.cmake`でECM/KCodecs/KMime 6.31.0のDebug DLLを指定Qt SDKに対してビルド・ローカルインストール済み。ソースとprefixはbuild/deps配下。KIMAP/KSMTP 26.08.3はソース確認のみ、KIO/CoreAddons/I18n/SASL構築は未完了。

## 次の作業

- 次はKDE依存の構築・検証。指定Qt SDK以外のQtを取得・ビルドしない。第三者ソースとビルド成果物はGit対象外のbuild配下へ分離する。
- 本文のURLポリシー（http/httpsのみ、userinfo禁止）は暫定仕様。URLデコード後の制御文字を拒否し、リンククリック時も再検査する。
- 表示時にはQTextDocumentの正規化後の文字列で範囲計算する。CRLFとUTF-16サロゲートペアの回帰テストを維持する。
- MimeParserは同期の値変換アダプター。GUIではなくメールワーカーから呼ぶこと。現在は画面への読込み未接続。原文をバイト列で保持し、KMimeオブジェクトをUIへ渡さない。
- 本文と添付メタデータを分離。MIME上限値と抽出時チェックの限界はspec/provisional/mime-limits.mdに記載。
- MimeParser追加後のDebugビルドとCTest全3ターゲットが成功。詳細ログはmimeparser-results.txtで15 passed（init/cleanupを含む）。日本語3文字コードとMIME転送符号化・添付名・抽出上限を確認。
- CoreAddons/KConfig/KCrash 6.31.0のDebug DLLを同一Qt SDK/MSVCに対して構築済み。BuildFrameworkDependencies.cmakeの共通処理を含む再実行も成功（build/deps/framework-bootstrap.log）。
- KIMAP/KSMTPにはKIOCoreが必要。KIOCORE_ONLYでもKService/Solid/Crashのfind_packageが必須。KI18nはlibintl、SolidはFlex/Bisonが必要で、Windows向けの依存取得・固定手順を続けて整備する。
- GitHub/Release assets/ftp.gnu.org/repo.msys2.orgの接続許可を承認済み。Craft blueprintはbuild/deps/src/craft-blueprints-kdeに調査用取得済み（2eb0b809261d66717cd3060d02065f3b21e70802）。Qtを取得するCraft実行はしない。
- 重要: KSMTP 26.08.3 loginjob.cpp:155以降で優先認証方式からLOGIN/PLAINへのfallbackがある。OAuthでは利用可能機構を検査してからジョブ開始し、欠如時はトークンを一切送らないテストが必須。
- BuildSaslDependencies/BuildXOAuth2DependencyでSASL 2.1.28と3機構をDebug DLL構築済み。CyrusのGitソースには生成ファイルがないためSHA256固定の公式tarballを使用。上流nmakeは子ディレクトリで引数が継承されないため、lib/pluginsを個別実行する。
- SASL単体テストにはWindowsのWSAStartupが必要。Qt Network経由ではなく生APIを呼ぶテストなので明示初期化している。全4 CTestターゲットが成功。
- WinFlexBison 2.5.25（1176da2adaf972b2216b1ac8dd611e128fe80bc5）をMSVCでソースビルド済み。BuildWinFlexBison.cmakeによる再実行も成功。Solid 6.31.0のDebug DLL構築も成功（solid-build.log）。Solid以降の手順のスクリプト化は後続作業。
- vcpkg-recipesは177e8175121536d414f405ed8b3099b325a42638、ツールは2026-10-29を固定。gettext-libintl/libiconvのDebug/Release構築・インストールが成功（execution 1a7ecddffd914c019253840369040033、build/deps/vcpkg-native.log）。
- vcpkg組込みHTTP/WinHTTPはサンドボックスのproxyに失敗。X_VCPKG_ASSET_SOURCESでPython helper→Git付属curlを使用して取得できた。証明書検証を無効にしたりproxy変数を消したりしない。helperのGNU/MSYS2 canonical URL変換とSHA512等の4テストは成功。
- BuildNativeDependencies.cmakeによるgettext[tools]の構築・インストールは成功（execution d9506241a0024247876a29f847b430b4、build/deps/native-tools-build.log）。KI18nのconfigureはlibintlだけでなくGettextのmsgfmt/msgmergeも必須。
- 参考用に取得したlibintl-liteはAPI/翻訳機能がGNU gettextと異なり、Windows代替として採用していない。製品へ組み込まない。
- authenticationDecisionを追加。TLS/証明書未検証、必須機構欠如、不明なenumを拒否する。KSMTPと同じ厳密な機構名で判定するため、大文字小文字を緩く許可してライブラリ側のfallbackを誘発しない。LoginJob作成前・秘密情報設定前にこの判定を適用すること。現在はポリシー単体テスト段階で接続層へ未接続。
- SASLを有効にしたdevビルドでCTest全5ターゲットが成功（connectionpolicy/mainwindow/messageviewer/mimeparser/sasl）。
- Native bootstrapはVSINSTALLDIR末尾の区切りを削除してVCPKG_VISUAL_STUDIO_PATHへ渡す。末尾区切りがあるとvcpkgのVS照合が失敗した。
- KIO 6.31.0のfavicon除外パッチはユーザ承認済み。patches/kioとTHIRD_PARTY_NOTICES.mdに明記し、BuildKioDependencies.cmakeで独立コピーへ適用。原本・Qtは変更しない。残るライセンス監査は未完了で、配布物全体がMITであるとは記載しない。
- BuildI18nDependencies.cmakeでKI18n/KService 6.31.0のDebug DLL構築に成功（execution adc1844f7c284914a852c024c54d31a2、i18n-build.log）。native Debugのlibintlとホスト用gettextのパスを明示指定。上流ソースは変更していない。
- KDE_BASE/SASL両テスト有効でCTest全6ターゲットが成功。テスト/実行PATHにはnative/x64-windows/debug/bin、native/x64-windows/tools/gettext/bin、install/Debug/bin、指定Qt/binが必要。ホストツールは自身のRelease DLLをexe隣からロードする。
- dumpbin /dependentsでKF6I18n.dllがQt6Cored/intl-8等、KF6Service.dllがQt6Cored/Qt6Xmld/ConfigCore/CoreAddons等を使用することを確認。これらのテストにKIOはリンクしていない。
- パッチ適用KIO 6.31.0のDebug構築が成功（kio-build.log）。KIOのcompile_commands、ninja -t deps（kio-headers.log）、dumpbin /exports（kio-exports.log）にfaviconscache実装・ヘッダ・クラスエクスポートが含まれないことを確認。対象favicon関数は空文字を返す。原本のgit statusはクリーン。
- KIOテストを有効にしたCTest全7ターゲット成功。パッチコピー保護のPythonテスト5件も成功。開発prefixにはGPL等のworker/補助実行ファイルがあるので、製品へ一括コピーしない。
- BuildMailDependencies.cmakeでKIMAP/KSMTP 26.08.3（package 6.8.3）Debug構築成功（build/deps/mail-build.log）。SMOOTHMAIL_TEST_MAILは合成loopback向けの開発用テストで既定OFF。コンパイル成功だが通信は未検証。Safe Shell内では最小Python TCP接続もWinError 10013となり、localhost許可設定でも接続不可。既存7ターゲットは成功、mailtransportは当初90秒timeout。現在はinitでloopback確認し早期失敗する。サンドボックス外実行はしていない。
- .gitattributesで*.patchをLF固定。KIOパッチのSHA256によるコピーキー・インストール済み通知照合がcheckoutの改行変換で変わらないようにする。
