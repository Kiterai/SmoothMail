# メール処理の依存構築

個別依存の構築・調査の引継ぎ資料です。過去の未完了記述や旧テスト手順を含みます。通常の構築は[ビルド手順](../../docs/dev/build.md)、現状の未完了項目は[未完了一覧](../../spec/inprogress/README.md)を優先してください。

Qtは開発者がインストールしたSDKを使用します。以下の処理はQtを取得せず、第三者ソース・成果物をリポジトリ内の`build/deps/`に配置します。

## Windowsの標準入口

作業者の準備は **CMake 3.30以降・Git・既存Qt（必要な開発モジュールを含む）・Python・MSVC/Windows SDK** です。MSVC x64開発者コマンドプロンプトで実行します。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildWindows.cmake
```

固定ソース取得→依存順のconfigure/build/install→アプリ構築を行います。依存だけなら`BuildWindowsDependencies.cmake`を使います。Releaseは`-DCMAKE_BUILD_TYPE=Release`。別SDK/コンパイラには新しい`-DDEPENDENCY_ROOT=...`と`-DAPPLICATION_BUILD=...`を使用してください。

- Ninja 1.13.1は固定Git commitから内部取得し、MSVC付属NMakeで構築します。手動インストールやPATH設定は不要です。
- vcpkg本体は固定URL/SHA512で内部取得します。用途はGNU gettext/libintl/libiconvのみで、Qtのport/toolchainは導入しません。
- アーカイブはCMakeでHTTPS取得・TLS証明書検証・ハッシュ検証します。途中ファイルを再利用せず、既存キャッシュの不一致は上書きせず停止します。プロキシ・CA設定はCMakeが使える環境にしてください。TLS検証を無効にする回避策はありません。
- プロジェクトのスクリプト・テストはCMake/C++です。PythonはKI18n上流のconfigure/翻訳データ生成からのみ間接利用します。
- SolidとそのFlex/Bisonも標準入口で構築します。変更対象7依存のパッチ・原本分離・receipt照合を維持します。
- 既存presetだけ使う場合は、最初に`cmake -DQt6_ROOT=... -P cmake/PrepareBuildTools.cmake`を実行します。カスタム依存rootでは`CMAKE_MAKE_PROGRAM`もその内部Ninjaへ指定してください。

以下は個別ステージの説明です。`CMAKE_MAKE_PROGRAM`の明示指定は既存環境向けの任意指定で、省略すると固定Ninjaを内部準備します。

### キャッシュなしの取得・構築検証

通常のMSVC x64開発者コマンドプロンプトで、**まだ存在しない短いディレクトリ**を指定します。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DDEPENDENCY_ROOT=C:/build/smoothmail-clean-deps -P cmake/VerifyWindowsCleanDependencies.cmake
```

固定Gitソースをすべて新規取得し、内部Ninja、vcpkg・GNU/MSYS2・SASLを含む標準のRelease依存構築を実行します。ダウンロード先・インストール先を新root内に隔離し、vcpkgのバイナリキャッシュと外部overlayは使いません。既存rootは空でも拒否し、失敗したrootを消去・上書きして再利用しません。再試験は別の新rootを指定してください。

成功時だけ`clean-verification-completed.json`を生成します。固定commit一覧・manifestのハッシュ・取得資材のSHA512を記録します。Qt SDK・MSVC/Windows SDK・CMake・Git・Pythonは作業者が準備する前提であり、それらの新規導入や、アプリ/ZIP・クリーンPCでの実行確認は別の検証です。

2026-11-14、固定Gitソース17件の新規取得と全Release依存構築が成功し、構築後のcommit・取得物一覧52ファイルのハッシュ再照合も通過しました。[検証結果](clean-dependency-verification.md)を参照してください。

TLS証明書検証は常に有効です。CMakeのstatus 35だけでCA不備とは断定せず、実行環境側のTLSエラーも確認してください。Safe Shell内のSchannel `SEC_E_NO_CREDENTIALS`では通常環境との比較が必要です。証明書検証の無効化や外部curlへの切替は行いません。

## MIME依存

CMake 3.30以降、Git、Qt 6.10以降が必要です。Windowsでは指定Qtと一致するMSVC x64開発環境で実行してください。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildMimeDependencies.cmake
```

- 明示的に実行した場合だけ、公式リポジトリからECM・KCodecs・KMimeを取得します。
- タグと実コミットは`cmake/dependencies.json`で固定します。既存ソースのコミット不一致・追跡ファイルの変更がある場合は停止し、上書きしません。
- DLLは`build/deps/install/Debug/bin`、CMake packageは同じprefixの`lib/cmake`等へ出力します。
- Release版には`-DCMAKE_BUILD_TYPE=Release`を指定します。Debugとは別prefixになります。
- 別Qt SDKや別コンパイラを使用する場合は`-DDEPENDENCY_ROOT=/path/to/new/deps`で新規ディレクトリを指定してください。
- 上流の自動テストはこのbootstrapでは実行しません。アプリ側の統合テストと別に検証が必要です。

指定SDK（Qt 6.12.0/MSVC 2022）でECM・KCodecs・KMime 6.31.0のDebugビルド・インストールに成功しています。クリーンPCへの配布検証は未実施です。

## KIMAP / KSMTP

### 構築済みの基礎Frameworks

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildFrameworkDependencies.cmake
```

MIME依存に加え、CoreAddons・KConfig・KCrash 6.31.0を構築します。QML/Pythonバインディング等の不要な機能は無効にします。Qt 6.12.0 / MSVC 2022でDebug DLLの構築・ローカルインストールを確認済みです。

共通の取得・コミット確認・変更検出・ビルド処理は`cmake/DependencyBuild.cmake`へ集約しています。

### 未完了の依存

26.08.3のソースと実コミットを確認し、manifestに記録しました。**上記のMIME専用bootstrapではビルドしません。**

上流CMakeで以下の直接依存を確認しています。

| 対象 | 主な直接依存 |
|---|---|
| KIMAP | Qt Network、KMime、CoreAddons、I18n、KIOCore、Cyrus SASL |
| KSMTP | Qt Network、CoreAddons、I18n、KIOCore、Cyrus SASL |
| KMime | Qt Core、KCodecs |

KIMAP/KSMTPの要求はQt 6.9以降、Frameworks 6.25以降、CMake 3.29以降です。ただし今回選んだKMime/KCodecs 6.31.0はQt 6.10以降・CMake 3.30以降が必要です。

KI18nのWindows構築にはlibintl、KIOにはKConfig/CoreAddons/I18nに加えてKService・Solid・KCrashが必要です。`KIOCORE_ONLY=ON`でも上流configureはKService/Solidを要求します。Solidにはビルド時のFlex/Bisonも必要です。これらの基礎依存は構築済みです。KIOCoreは下記の承認済み限定パッチを適用して構築します。

### gettextとビルド用ツール

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildWinFlexBison.cmake
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildNativeDependencies.cmake
```

WinFlexBison 2.5.25と、それを使うSolid 6.31.0のMSVC構築は確認済みです。NativeDependenciesは固定vcpkgレシピとハッシュ固定のvcpkg実行ファイルを使います。製品向けGNU DLLは`build/deps/native/gnu-runtime-v1`へ別構築し、gettextホストツールは従来の`build/deps/native`で管理します。vcpkgでQtを導入したり、アプリへvcpkgのQt toolchainを指定したりはしません。

`cmake/native-triplets/x64-windows.cmake`は同じx64・dynamic CRT/DLL構成を維持し、PDB参照名を固定、gettext-libintlの論理configure prefixをRelease `/`・Debug `/debug`へ設定します。lib/dataディレクトリも明示し、MSYSの該当-D引数の変換を抑止して、ビルド先の再混入と`//share`のUNC解釈を避けます。実ファイルはvcpkgのDESTDIRで専用prefixへ収集され、PCのルートへインストールされるわけではありません。GNUの新しいソースパッチやバイナリ書換え、検査除外は使いません。

製品向けrootではレシピcommit・ツール・tripletの設定ハッシュを保存し、別設定のインストール済み資材を黙って再利用しません。設定を変更する場合は新しい`-DSMOOTHMAIL_NATIVE_ROOT=...`を選び、全ステージで同じ値を指定してください。ホストツールrootは`SMOOTHMAIL_GETTEXT_TOOLS_ROOT`で別管理し、両rootの混在を拒否します。Native構築後は製品向けRelease DLLを既存の個人パス検査にかけます。

GNUの合成回帰は`SMOOTHMAIL_TEST_NATIVE=ON`で有効にできます。DLLと合成カタログを別場所（空白・日本語を含む）へコピーし、Shift-JIS変換、不正入力の拒否、UTF-8訳文・複数形を確認します。カタログはKI18nと同様に**移動先のパスを明示してbind**する方式です。論理prefixだけで任意の配布先を自動検索できるという保証ではありません。

vcpkgの資材取得も常にCMakeのx-scriptを使い、外部curl/Pythonやorigin fallbackは使用しません。GNU/MSYS2のミラーURLを各公式配布元へ正規化します。オフラインガードは`cmake -P tests/cmake/run_build_guards.cmake`で実行できます。既存キャッシュだけで成功した場合は、新規ネットワーク取得の成功を意味しません。

gettext/Flex/Bison等のGPLビルドツールは、アプリへリンクするLGPLのlibintlとは区別します。これらのビルドツール自体を製品へ同梱する構成にはしません。

### KI18n / KService

基礎Frameworksとネイティブ依存の構築後に実行します。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildI18nDependencies.cmake
```

スクリプトはDebug/Releaseに合わせたlibintlを明示指定し、gettextホストツールとは分離します。別prefixを使う場合は`-DSMOOTHMAIL_NATIVE_ROOT=/path/to/new/native`を全体で指定してください。Intlだけに別の`SMOOTHMAIL_NATIVE_PREFIX`を指定して混在させることは拒否します。

ロード・基本APIのテストは次で有効にできます（既存の開発用PATHに追加）。

```bat
set PATH=%CD%\build\deps\native\gnu-runtime-v1\x64-windows\debug\bin;%CD%\build\deps\native\x64-windows\tools\gettext\bin;%PATH%
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DSMOOTHMAIL_TEST_KDE_BASE=ON
cmake --build --preset dev
ctest --preset dev
```

KI18nによる英語フォールバックの引数・複数形処理、KServiceのメタデータ生成を確認します。サービスの実行・登録・デスクトップDBの検索は行いません。KIOをリンクするテストではありません。日本語翻訳カタログやクリーンPCへの配布は別途検証が必要です。

### KIOCoreの暫定パッチとライセンス通知

取得したKIO 6.31.0の`src/core/CMakeLists.txt`は`faviconscache.cpp`を無条件にライブラリへ組み込みます。同ファイルと`faviconscache_p.h`のSPDXは`GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL`であり、LGPLの選択肢は記載されていません。

利用者の承認を得て、メールに不要なfavicon機能を除外する限定パッチを暫定適用しています。ライセンス表記は変更しません。変更内容・失われる機能・配布上の注意点は`THIRD_PARTY_NOTICES.md`と`patches/kio/README.md`に明記しています。残るビルド対象・依存の配布監査は未完了です。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildKioDependencies.cmake
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DSMOOTHMAIL_TEST_KIO=ON
cmake --build --preset dev
ctest --preset dev
```

原本は変更せず、`build/deps/patched-src`に独立コピーを作成して`git apply --check`後にパッチを適用します。コピーの予期しない変更は上書きせず拒否します。`cmake -P tests/cmake/run_build_guards.cmake`で、その保護と再実行をネットワークなしでテストできます。

指定Qt/MSVCでのKIOCore構築、favicon無効化・SSLエラー情報APIのテスト、対象実装・ヘッダがcompile commands／Ninja依存情報／DLLエクスポートに含まれないことを確認済みです。上流のライセンス文書・変更パッチ・変更通知はprefixの`share/licenses/kio`へ配置します。

**この手順の開発用prefixにはKIO worker・補助実行ファイルも入ります。製品配布時にprefix全体を一括コピーしてはいけません。** 製品で必要なDLL・プラグインだけを特定し、各コード・生成物・推移依存の条件を別途確認します。

確認箇所: `build/deps/src/kio/src/core/{CMakeLists.txt,faviconscache.cpp,faviconscache_p.h,global.cpp}`。上流: https://invent.kde.org/frameworks/kio/-/tree/v6.31.0/src/core

### KIMAP／KSMTPの構築とローカル検証

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildMailDependencies.cmake
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DSMOOTHMAIL_TEST_MAIL=ON
cmake --build --preset dev
ctest --preset dev
```

KIMAP／KSMTP 26.08.3（CMakeパッケージ版6.8.3）は指定Qt/MSVCでDebug構築済みです。先に承認済みKIOパッチとSASL機構を構築してください。実行時はQt・依存prefix・native依存のbinをPATHに追加します。

KIMAPには`patches/kimap/26.08.3-fix-sasl-continuation.patch`を適用します。空のSASLチャレンジ処理と、不正応答時のエラーコード未設定を修正するローカルパッチです。原本の独立コピーを使用し、変更通知・上流ライセンス・差分は`share/licenses/kimap`へ配置します。詳細は`patches/kimap/README.md`と`THIRD_PARTY_NOTICES.md`を参照してください。

KSMTPにも`patches/ksmtp/26.08.3-reset-ehlo-capabilities.patch`を適用します。STARTTLS後に以前のAUTH等の広告情報を残さないための修正です。未修正版ではTLS後にXOAUTH2が広告されなくてもAUTHを送ることを合成サーバーで再現し、修正後はAUTHと秘密情報を送らず拒否することを確認しています。差分・通知は`share/licenses/ksmtp`へ配置します。

変更済みKIO/KIMAP/KSMTP/KMime/KCodecsとWindows向けKConfig/KCrashの構築時に`share/licenses/<dependency>/smoothmail-build.json`を生成します。configure時に固定commit、patch SHA-256、実DLL SHA-256、Qt Core DLL SHA-256、ビルド構成を照合し、不足・不一致を拒否します。JSONはデータとしてのみ読み込み、実行しません。ローカルの取り違え防止であり、署名や信頼できない配布元の真正性保証ではありません。更新後は依存構築スクリプトを再実行してください。`cmake -P tests/cmake/run_build_guards.cmake`で検証できます。

### WindowsのKCrash/KIOの実行時パス

KCrashは`BuildKcrashDependencies.cmake`を経由し、DrKonqi探索からビルド時prefixだけを除きます。
KIOはfavicon除外を含む`6.31.0-smoothmail-portable-core.patch`を使い、Windowsの補助ツール探索を実行時配置に限定/変更し、admin workerは明示拒否します。元prefixにだけあるhelperの利用はできなくなります。
経緯・機能制限・ライセンス・撤去条件は[KCrashパッチ説明](../../patches/kcrash/README.md)と[KIOパッチ説明](../../patches/kio/README.md)を参照してください。

既存Windows prefixはKCrash→KIOの順で再構築してください。KCrash receiptがない場合や、KIO receiptが旧favicon単独差分の場合はアプリconfigureを拒否します。
`SMOOTHMAIL_TEST_KIO=ON`では合成helper/プラグインを使う`portablekde`も登録します。DrKonqiや外部workerは起動せず、admin拒否・helper解決/未配置エラー・TLSエラー情報等を確認します。
深いworktreeではWindowsの生成ヘッダincludeがパス長制限に当たることがあります。その場合は短い独立`DEPENDENCY_ROOT`を指定してください。

### Windows向けKConfigの限定パッチ

`BuildFrameworkDependencies.cmake`は`BuildKconfigDependencies.cmake`を経由し、Windows Debug/Releaseの両方で`KCONFIG_USE_KCONF_UPDATE=OFF`として構築します。`checkUpdate()`の公開APIは維持しますが、設定移行は無効です。経緯・採用理由・ライセンス条件・撤去条件は[パッチ説明](../../patches/kconfig/README.md)、配布向けの要約は[第三者通知](../../THIRD_PARTY_NOTICES.md)を参照してください。

既存prefixに新しいreceiptがなければ、アプリの再configure前にこの依存構築を実行してください。開発prefixに残る`kconf_update.exe`は配布対象にせず、prefix全体を製品へコピーしません。

KCodecs 6.31.0にも`patches/kcodecs/6.31.0-safe-ctype.patch`を適用します。RFC 2047デコーダーの`isspace`引数をunsigned charへ変換します。`BuildMimeDependencies.cmake`がECM、修正KCodecs、修正KMimeの順に構築します。検証済みDebug構築先は`build/deps/build/Debug/kcodecs-safe-ctype-msvc-debug`。上流LICENSES・差分・変更通知・receiptを`share/licenses/kcodecs`へ配置し、Qt SDKは変更しません。

KMime 6.31.0には`patches/kmime/6.31.0-safe-ctype.patch`を適用します。非ASCIIバイトをC文字判定関数へ渡す際のunsigned char変換で、Debug CRTアサートと未定義動作を修正します。MIME bootstrapは独立した修正ソースから構築し、WindowsではMSVCコンパイラー/リンカーを明示します。検証済みDebugビルドは`build/deps/build/Debug/kmime-safe-ctype-msvc-debug`で、Qt SDKは変更していません。上流ライセンスと変更通知を保持します。

`mailtransport`はループバック限定の合成サーバーとダミー資格情報を使用します。PLAIN／LOGIN／XOAUTH2の通信、未広告のOAuth方式の拒否、STARTTLS非対応時の認証禁止を対象とし、認証成功ケースは明示的な非暗号化設定です。実サービス、TLSハンドシェイク・証明書検証、Google／MicrosoftのOAuth認可フローの検証ではありません。

現在のSafe Shell内ではループバックTCP接続が拒否されます（Pythonの最小接続でも`WinError 10013`）。localhostが設定上許可されていても、この環境では接続できません。起動時の接続確認で早期に失敗させ、各ケースのタイムアウトを避けます。

2026-11-13、別のWindows Sandbox（外部ネットワーク無効）内ではループバック通信を実行できました。KIMAP修正後は**15 passed／0 failed**（init／cleanupを含む）です。同じ拡張テストを未修正DLLでも実行し、12 passed／3 failedを確認しました。

- 成功: SMTPのPLAIN／LOGIN／XOAUTH2、IMAPのPLAIN／LOGIN／XOAUTH2、SMTPでXOAUTH2が未広告の場合の認証禁止、両プロトコルのSTARTTLS非対応時の認証禁止。
- IMAPのXOAUTH2は、`+ `、`+`、`+ =`の空チャレンジで成功を確認しました。不正な応答と認証拒否では、非0エラー・未認証状態・別方式への再試行なしを確認しています。
- 未修正DLLでは、XOAUTH2の空チャレンジ2ケースと、不正応答時のエラーコード検査が失敗しました。以前の10 passed／1 failedはこの問題によるものです。今後もエラー値だけでなくセッションの認証済み状態を確認します。
- 比較ログ: `build/sandbox-mail-output/{patched,unpatched}-results.txt`、終了コードはそれぞれ0／3です。以前の検証ログ`mailtransport-results.txt`は未修正時の記録として残しています。

実行には、テストEXE・依存DLL・SASLプラグイン・Qt Debug DLL／TLSプラグイン・MSVC Debug CRT／Debug UCRTを`build/sandbox-mail-test`へ集め、読み取り専用共有からゲストの`C:\MailTest`へコピーしました。`SMOOTHMAIL_TEST_SASL_PATH`をゲストの`sasl2`に、`QT_PLUGIN_PATH`をゲストの実行ディレクトリに指定し、Qt Testの`-o`でログを書き込み可能な専用共有へ回収しました。これは開発用の検証セットで、製品配布物ではありません。特にDebug CRTは製品へ同梱しません。ゲストはログ回収後に終了済みです。

### 認証方式の切替に注意

KSMTP 26.08.3の`LoginJobPrivate::selectAuthentication()`は、優先認証方式が広告されていない場合にLOGIN/PLAINを選ぶ実装です。アプリのMailServiceではログインジョブの開始前に、サーバーが設定された方式を広告しているか確認しています。特にOAuthトークンを別方式へ自動送信してはいけません。XOAUTH2を広告しないローカルサーバーへ、`authenticationDecision()`による拒否後にAUTHやトークンを送らない回帰テストはWindows Sandbox内で成功しました。実サービスでは未検証です。

## WindowsのSASLクライアント

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildSaslDependencies.cmake
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildXOAuth2Dependency.cmake
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DSMOOTHMAIL_TEST_SASL=ON
cmake --build --preset dev
ctest --preset dev
```

MSVC開発環境で実行し、Qtと依存prefixのbinをPATHへ追加してください。取得にはCMakeを使用します。スクリプトはWindowsのDebug/Release向けです。

- Cyrus SASL 2.1.28は公式リリースアーカイブをSHA256検証して取得します。生成済みソースを含むアーカイブが必要なため、Gitソースとは別に展開します。
- 必要なPLAIN/LOGINだけを構築・配置します。SASLデータベース・サーバー管理ツール・不要な機構は配布対象にしません。
- 2.1.28にXOAUTH2は含まれないため、MITの`moriyoshi/cyrus-sasl-xoauth2`を実コミット固定で別途取得します。MSVC/API互換用ヘッダとクライアント専用エントリーを追加しますが、上流のプロトコル実装は変更しません。
- プラグインは`build/deps/install/Debug/bin/sasl2`に配置します。テストはこのパスを明示指定し、グローバルのSASL設定やレジストリは変更しません。
- SASL単体テストで3機構のロード、PLAIN資格情報、LOGINチャレンジ、XOAUTH2応答、未知の機構の拒否を確認しています。実サーバーの認証成功・TLS・OAuth認可フローを検証したという意味ではありません。
- 各ライセンスはprefixの`share/licenses`へ配置します。全依存の配布監査は別途必要です。

## ライセンスと残る検証

Windowsの開発用起動フォルダを作成する`StageWindowsRuntime.cmake`と`windeployqt`の手順は[実行手順](../../docs/dev/run.md)を参照してください。記録当時の起動用cmdは`build/start-smoothmail.cmd`（`build/local-run`を起動）です。Debug CRTを含む開発テスト専用で、製品配布物ではありません。以前の`build/launch-debug`は別の過去検証用フォルダです。

秘密情報保存用のQtKeychainは、次の手順で指定Qt SDKに対して構築します（別Qtの取得なし）。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildKeychainDependencies.cmake
```

0.15.0の固定ソースを無変更でDebug DLLとして構築済みです。アプリでは平文フォールバックを明示的に禁止します。Windowsバックエンドの動作検証は、ホストの資格情報を変更しないようWindows Sandbox内で行います。

Windows SandboxでQtKeychainの日本語・8192文字の保存/読出し/削除、保存上限超過時のアカウント登録のロールバックを検証済みです。テスト実行には`SMOOTHMAIL_TEST_KEYCHAIN=1`が必要です。ホストでは既定でスキップし、実資格情報に触れません。

アプリの接続テストでは、実行ファイル隣の`sasl2`を明示指定します。開発時は`SMOOTHMAIL_SASL_PATH`で配置先を明示的に指定できます。ディレクトリ欠如時は失敗とし、グローバルのプラグイン設定へ切り替えません。

`connectiontester`は実アダプターを専用ワーカーで動かすテストです。Sandbox内で11 passed。PLAIN/LOGIN/XOAUTH2、未広告方式の拒否、STARTTLS非対応時の拒否、SSL/TLS・STARTTLSの信頼済み証明書での成功、未信頼証明書での失敗と秘密情報非送信を確認済みです。信頼証明書はテストプロセス内だけに追加し、OSの証明書ストアは変更しません。`tests/data/README.md`にダミー証明書・鍵の生成方法を記載しています。

IMAPはライブラリAPI上、TLSハンドシェイクとログインを別ジョブに分離できません。そのためTLS/STARTTLSでは、証明書エラーを必ず拒否するproxyとKIMAP内部の暗号化確立後のCAPABILITY/機構チェックを利用します。平文を明示したIMAPとSMTPはLoginJobへ秘密を設定する前に`authenticationDecision()`で機構を検査します。成功判定はエラーコードだけでなくセッション状態と要求暗号化方式も確認します。

KMimeのライブラリソースにLGPL-2.0-or-later、KSMTPのライブラリソースにLGPL-2.1-or-laterとLGPL-2.0-or-laterを確認しています。CMakeのライセンスチェック引数だけで製品ライセンスを判定しないでください。

全推移依存・生成物・翻訳・同梱コードのライセンス監査は未完了です。配布前には告知、対応ソース提供、差替え可能性等を整備します。SASL機構ロード・KIMAP/KSMTP構築・合成サーバーTLSとSDK未導入guestでの開発用実行は確認済みですが、実サービス認証とRelease製品配布は引き続き検証対象です。
