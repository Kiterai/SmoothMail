# GNU DLLの個人パスをビルド設定で除去

2026-11-14。対象は固定vcpkgレシピのlibiconv 1.19 / gettext-libintl 0.22.5。

## 原因と採用設定

- 親の`DependencyBuild.cmake`で指定したリンカflagsは、vcpkg-makeで別途構築するGNU portには伝わらない。
- `VCPKG_LINKER_FLAGS`はvcpkg-makeのMSVC linker wrapperへ渡るため、
  `/PDBALTPATH:smoothmail-native.pdb`でCodeViewのPDB参照を相対名へできる。
- gettextのLOCALEDIR/INSTALLDIR/INSTALLPREFIXはconfigure/Makefileのインストール先に由来する。
  `--enable-relocatable`だけでは元prefix文字列は消えない。
- `VCPKG_MAKE_CONFIGURE_OPTIONS_RELEASE=--prefix=/`、
  `VCPKG_MAKE_CONFIGURE_OPTIONS_DEBUG=--prefix=/debug`をgettext-libintlだけに追加すると、
  論理的なターゲットprefixと実際のvcpkgインストール先を分離できる。
- 実インストールはvcpkg-makeの`DESTDIR=CURRENT_PACKAGES_DIR`で隔離される。
  通常のbin/lib/include配置とGNUの再配置機能は維持する。PCのルートへインストールしない。
- prefixだけを`/`にすると`//share/...`ができてWindowsでUNC解釈され得るため、
  libdir/datarootdirも単一スラッシュの論理パスに明示する。
  MSYS2_ARG_CONV_EXCLでLOCALEDIR/INSTALLDIRの-Dまたは/D引数だけの自動変換を止め、
  それらがMSYSの実配置先へ変わることを防ぐ。MSYSでのcl前処理試験も成功。
- GNUの新しいソースパッチ、DLL文字列の事後書換え、検査除外は追加していない。

## キャッシュ・構築経路

- `cmake/native-triplets/x64-windows.cmake`はstock tripletと同じx64/dynamic CRT/dynamic libraryのABI。
- 製品向けrootは`build/deps/native/gnu-runtime-v1`。ホストツールは従来の`build/deps/native`で別管理し、
  両rootを同じ値へ指定することは拒否する。実験prefixは採用せず保存する。
- triplet/レシピcommit/vcpkg tool digestの設定ハッシュをrootに保存。
  設定が違う既存prefixや、識別不能な非空prefixを黙って使わず拒否する。
  設定変更時は新しい`SMOOTHMAIL_NATIVE_ROOT`を選ぶ。既存ファイルは削除しない。
- Native ROOTは依存構築・I18n/KIO/Mail・アプリ・配布収集に共通適用。
  Intlだけ異なるprefixを指定して混在させる旧オプションは拒否する。
- Native構築後にRelease bin全体を既存C++配布検査へ渡す。Qt/GNU以外の検査規則は変更しない。

## 実験と試験

最初に別root`build/deps/native-privacy-experiment`でPDB/論理prefixだけの設定を検証した。
libiconvとgettext-libintlのDebug/Release構築・インストール成功。
Release bin全体は既存のPE/個人パス検査を通過し、列挙診断も空だった。
旧prefixのDLL/旧収集runtimeは残している。

`tests/native-runtime`は指定したDLLの実ロード元を確認してから公開APIを呼ぶ。

- Shift-JIS→UTF-8（「あ」）の変換。
- 不完全なマルチバイト入力の拒否。
- プロセス内だけでテストlocaleを設定し、独自生成の`.mo`から日本語の訳文を取得。
- 複数形の1/2の双方を検証。
- DLLと合成カタログを空白・日本語を含む別フォルダへコピーし、別プロセスで同じ試験。
- 作業ディレクトリをDLLの場所とは無関係な使い捨てフォルダへ変えて、CWD依存を避ける。

旧DLLでも機能試験の対照が成功、prefixだけの初期設定DLLでも両配置で成功。
ただし、後述のUNC既定パスの追加検証ではこの初期設定DLLを拒否し、最終設定には採用しない。
カタログは**KI18nのkcatalog.cppと同様に、移動先を明示してbindtextdomainする方式**。
論理prefixから任意の製品配置先を自動検索できるという試験/保証ではない。
既存のKi18n利用を、gettext既定ディレクトリへの暗黙依存へ変更しない。

初期profile `native/privacy-v1`ではGNU Debug/Releaseとgettextホストツールを構築できた。
初回は最後の検査起動でPATHのセミコロンが汎用command関数の引数に分割されたため停止したが、
プロセス内ENVでPATHを設定する形へ修正し、再実行はビルド済み資材を再利用して成功。
NativeのRelease bin全体の個人パス検査も成功した。

KI18n/KServiceを両構成で新prefixへ再接続し、アプリのDebug/Releaseを再構築。
ホスト19 CTest対象が成功（従来18 + 新しいGNU再配置試験）。
Debug QtとRelease GNUの異なるCRT間ではqputenvだけだとLANGUAGEの環境配列が一致しないため、
合成試験は子プロセス生成時にLANGUAGE=zzを渡す。製品の動作変更ではなく試験の環境固定である。

初期profileの収集先は`build/gnu-settings-release-runtime`。
同梱GNU2 DLLと新prefixのSHA256一致、旧prefixと対照コピーのSHA256一致を確認した。
旧/新のexport名一覧も一致（iconv-2.dll: 11、intl-8.dll: 37）。
これだけで全APIの互換性を保証するものではないが、tripletのABI設定と代表的な公開APIの試験も維持している。
列挙検査でGNUの検出は0件、ランタイム全体の拒否は15件から13件へ減少した。
KCrash/KIO/Qt/SASLの既存問題はそのまま残し、他のDLLや検査規則は変更していない。

追加検証で既定localeの二重スラッシュを検出したため、上記の単一スラッシュ/MSYS変換抑止設定へ修正。
テストにはbind前の既定値が`/share/gettext-libintl/locale`に一致するガードを追加し、
初期設定DLLで失敗することも確認した。Native bootstrapでもこの試験を必ず実行する。

## 最終設定の検証結果

`native/gnu-runtime-v1`で以下を再実施し、初期profileの結果とは独立に成功した。

- 標準Native bootstrapのRelease DLL監査と、強制実行する既定パス確認・2配置の機能試験。
- KI18n/KServiceのDebug/Release再構築と、アプリ両構成の再configure/build。
- ホスト19 CTest対象すべて成功。実認証等の`oauthclient|connectiontester|mailtransport`は対象外。
- Releaseを`build/gnu-runtime-final`へ新規収集。GNU DLLのSHA256は最終prefixと一致。
- GNU2件の検出は0件、全体の拒否は13件。配布検査規則は変更していない。
- 旧prefixのGNU DLLは対照コピーとSHA256一致し、非変更。
- 旧/最終のexport名一覧が一致（iconv: 11、intl: 37）。
- オフライン構築ガード成功、vcpkgレシピ原本のtracked変更なし。

ログ: `build/gnu-final-bootstrap.log`、`build/gnu-final-consumers.log`、
`build/gnu-final-debug-build.log`、`build/gnu-final-release-build.log`、
`build/gnu-final-ctest.log`、`build/gnu-final-stage.log`、
`build/gnu-final-findings.json`。

オフライン設定ガードは新規/一致の受理と、設定変更/識別不能prefixの拒否の4ケース。
拒否時に既存のreceipt・合成ユーザーファイルを上書きしないことも確認した。
実メール・資格情報・実アカウントへのアクセスはしていない。

## ライセンス・残件

元のGNUソース・vcpkgの既存パッチと各ライセンスはそのまま維持する。
設定だけの変更でも、配布DLLに対応するソースと必要な構築スクリプトの提供などの義務は残る。
新triplet/設定ファイルも構築方法として保管し、gettextホストツールは製品へ同梱しない。

全体のQt/KCrash/KIO/SASL等の検査問題、legal監査、クリーンWindows配布検証、キャッシュなし全取得は別件。
