# メール処理の依存構築

Qtは開発者がインストールしたSDKを使用します。以下の処理はQtを取得せず、第三者ソース・成果物をリポジトリ内の`build/deps/`に配置します。

## MIME依存

CMake 3.30以降、Git、Ninja、Qt 6.10以降が必要です。Windowsでは指定Qtと一致するMSVC x64開発環境で実行してください。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildMimeDependencies.cmake
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
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildFrameworkDependencies.cmake
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
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildWinFlexBison.cmake
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildNativeDependencies.cmake
```

WinFlexBison 2.5.25と、それを使うSolid 6.31.0のMSVC構築は確認済みです。NativeDependenciesは固定したvcpkgレシピとハッシュ固定のvcpkg実行ファイルを使い、GNU gettext/libiconvとgettextホストツールを`build/deps/native`へ構築する手順です。**gettext-libintlとlibiconvのDebug/Release、およびgettextホストツールの構築・インストール成功を確認済みです。** vcpkgでQtを導入したり、アプリへvcpkgのQt toolchainを指定したりはしません。

サンドボックスのプロキシでvcpkg内蔵ダウンローダーが失敗する場合は、接続可能なcurlを指定して次を追加できます。

```text
-DSMOOTHMAIL_USE_ASSET_DOWNLOADER=ON -DCURL_EXECUTABLE=C:/path/to/curl.exe
```

このオプションにはPython 3.11以降が必要です。既存のプロキシ設定は変更せず、GNU/MSYS2のミラーURLを各公式配布元へ正規化し、HTTPS・証明書検証とSHA512確認を維持します。ダウンローダーの単体テストは`python tests/test_asset_fetch.py`で実行できます（ネットワークを使用しません）。

gettext/Flex/Bison等のGPLビルドツールは、アプリへリンクするLGPLのlibintlとは区別します。これらのビルドツール自体を製品へ同梱する構成にはしません。

### KI18n / KService

基礎Frameworksとネイティブ依存の構築後に実行します。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildI18nDependencies.cmake
```

KI18n/KService 6.31.0のDebug DLL構築に成功しています。スクリプトはDebug/Releaseに合わせたlibintlを明示指定し、gettextホストツールとは分離します。別prefixを使う場合は`-DSMOOTHMAIL_NATIVE_PREFIX=/path/to/native/x64-windows`を指定してください。

ロード・基本APIのテストは次で有効にできます（既存の開発用PATHに追加）。

```bat
set PATH=%CD%\build\deps\native\x64-windows\debug\bin;%CD%\build\deps\native\x64-windows\tools\gettext\bin;%PATH%
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DSMOOTHMAIL_TEST_KDE_BASE=ON
cmake --build --preset dev
ctest --preset dev
```

KI18nによる英語フォールバックの引数・複数形処理、KServiceのメタデータ生成を確認します。サービスの実行・登録・デスクトップDBの検索は行いません。KIOをリンクするテストではありません。日本語翻訳カタログやクリーンPCへの配布は別途検証が必要です。

### KIOCoreの暫定パッチとライセンス通知

取得したKIO 6.31.0の`src/core/CMakeLists.txt`は`faviconscache.cpp`を無条件にライブラリへ組み込みます。同ファイルと`faviconscache_p.h`のSPDXは`GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL`であり、LGPLの選択肢は記載されていません。

利用者の承認を得て、メールに不要なfavicon機能を除外する限定パッチを暫定適用しています。ライセンス表記は変更しません。変更内容・失われる機能・配布上の注意点は`THIRD_PARTY_NOTICES.md`と`patches/kio/README.md`に明記しています。残るビルド対象・依存の配布監査は未完了です。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildKioDependencies.cmake
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DSMOOTHMAIL_TEST_KIO=ON
cmake --build --preset dev
ctest --preset dev
```

原本は変更せず、`build/deps/patched-src`に独立コピーを作成して`git apply --check`後にパッチを適用します。コピーの予期しない変更は上書きせず拒否します。`python tests/test_kio_patch.py`で、その保護と再実行をネットワークなしでテストできます。

指定Qt/MSVCでのKIOCore構築、favicon無効化・SSLエラー情報APIのテスト、対象実装・ヘッダがcompile commands／Ninja依存情報／DLLエクスポートに含まれないことを確認済みです。上流のライセンス文書・変更パッチ・変更通知はprefixの`share/licenses/kio`へ配置します。

**この手順の開発用prefixにはKIO worker・補助実行ファイルも入ります。製品配布時にprefix全体を一括コピーしてはいけません。** 製品で必要なDLL・プラグインだけを特定し、各コード・生成物・推移依存の条件を別途確認します。

確認箇所: `build/deps/src/kio/src/core/{CMakeLists.txt,faviconscache.cpp,faviconscache_p.h,global.cpp}`。上流: https://invent.kde.org/frameworks/kio/-/tree/v6.31.0/src/core

### KIMAP／KSMTPの構築とローカル検証

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildMailDependencies.cmake
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DSMOOTHMAIL_TEST_MAIL=ON
cmake --build --preset dev
ctest --preset dev
```

KIMAP／KSMTP 26.08.3（CMakeパッケージ版6.8.3）は指定Qt/MSVCでDebug構築済みです。先に承認済みKIOパッチとSASL機構を構築してください。実行時はQt・依存prefix・native依存のbinをPATHに追加します。

KIMAPには`patches/kimap/26.08.3-fix-sasl-continuation.patch`を適用します。空のSASLチャレンジ処理と、不正応答時のエラーコード未設定を修正するローカルパッチです。原本の独立コピーを使用し、変更通知・上流ライセンス・差分は`share/licenses/kimap`へ配置します。詳細は`patches/kimap/README.md`と`THIRD_PARTY_NOTICES.md`を参照してください。

KSMTPにも`patches/ksmtp/26.08.3-reset-ehlo-capabilities.patch`を適用します。STARTTLS後に以前のAUTH等の広告情報を残さないための修正です。未修正版ではTLS後にXOAUTH2が広告されなくてもAUTHを送ることを合成サーバーで再現し、修正後はAUTHと秘密情報を送らず拒否することを確認しています。差分・通知は`share/licenses/ksmtp`へ配置します。

変更済みKIO/KIMAP/KSMTPの構築時に`share/licenses/<dependency>/smoothmail-build.json`を生成します。configure時に固定commit、patch SHA-256、実DLL SHA-256、Qt Core DLL SHA-256、ビルド構成を照合し、不足・不一致を拒否します。JSONはデータとしてのみ読み込み、実行しません。ローカルの取り違え防止であり、署名や信頼できない配布元の真正性保証ではありません。更新後は依存構築スクリプトを再実行してください。`python tests/test_modified_receipt.py`で検証できます。

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
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildSaslDependencies.cmake
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildXOAuth2Dependency.cmake
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DSMOOTHMAIL_TEST_SASL=ON
cmake --build --preset dev
ctest --preset dev
```

MSVC開発環境で実行し、Qtと依存prefixのbinをPATHへ追加してください。必要に応じて`-DCURL_EXECUTABLE=/path/to/curl.exe`を指定できます。スクリプトはWindowsのDebug/Release向けです。

- Cyrus SASL 2.1.28は公式リリースアーカイブをSHA256検証して取得します。生成済みソースを含むアーカイブが必要なため、Gitソースとは別に展開します。
- 必要なPLAIN/LOGINだけを構築・配置します。SASLデータベース・サーバー管理ツール・不要な機構は配布対象にしません。
- 2.1.28にXOAUTH2は含まれないため、MITの`moriyoshi/cyrus-sasl-xoauth2`を実コミット固定で別途取得します。MSVC/API互換用ヘッダとクライアント専用エントリーを追加しますが、上流のプロトコル実装は変更しません。
- プラグインは`build/deps/install/Debug/bin/sasl2`に配置します。テストはこのパスを明示指定し、グローバルのSASL設定やレジストリは変更しません。
- SASL単体テストで3機構のロード、PLAIN資格情報、LOGINチャレンジ、XOAUTH2応答、未知の機構の拒否を確認しています。実サーバーの認証成功・TLS・OAuth認可フローを検証したという意味ではありません。
- 各ライセンスはprefixの`share/licenses`へ配置します。全依存の配布監査は別途必要です。

## ライセンスと残る検証

Windowsの開発用起動フォルダを作成する`StageWindowsRuntime.cmake`と`windeployqt`の手順は`windows-launch.md`を参照してください。現在の起動用cmdは`build/start-smoothmail.cmd`（`build/local-run`を起動）です。Debug CRTを含む開発テスト専用で、製品配布物ではありません。以前の`build/launch-debug`は別の過去検証用フォルダとして残しています。

秘密情報保存用のQtKeychainは、次の手順で指定Qt SDKに対して構築します（別Qtの取得なし）。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -P cmake/BuildKeychainDependencies.cmake
```

0.15.0の固定ソースを無変更でDebug DLLとして構築済みです。アプリでは平文フォールバックを明示的に禁止します。Windowsバックエンドの動作検証は、ホストの資格情報を変更しないようWindows Sandbox内で行います。

Windows SandboxでQtKeychainの日本語・8192文字の保存/読出し/削除、保存上限超過時のアカウント登録のロールバックを検証済みです。テスト実行には`SMOOTHMAIL_TEST_KEYCHAIN=1`が必要です。ホストでは既定でスキップし、実資格情報に触れません。

アプリの接続テストでは、実行ファイル隣の`sasl2`を明示指定します。開発時は`SMOOTHMAIL_SASL_PATH`で配置先を明示的に指定できます。ディレクトリ欠如時は失敗とし、グローバルのプラグイン設定へ切り替えません。

`connectiontester`は実アダプターを専用ワーカーで動かすテストです。Sandbox内で11 passed。PLAIN/LOGIN/XOAUTH2、未広告方式の拒否、STARTTLS非対応時の拒否、SSL/TLS・STARTTLSの信頼済み証明書での成功、未信頼証明書での失敗と秘密情報非送信を確認済みです。信頼証明書はテストプロセス内だけに追加し、OSの証明書ストアは変更しません。`tests/data/README.md`にダミー証明書・鍵の生成方法を記載しています。

IMAPはライブラリAPI上、TLSハンドシェイクとログインを別ジョブに分離できません。そのためTLS/STARTTLSでは、証明書エラーを必ず拒否するproxyとKIMAP内部の暗号化確立後のCAPABILITY/機構チェックを利用します。平文を明示したIMAPとSMTPはLoginJobへ秘密を設定する前に`authenticationDecision()`で機構を検査します。成功判定はエラーコードだけでなくセッション状態と要求暗号化方式も確認します。

KMimeのライブラリソースにLGPL-2.0-or-later、KSMTPのライブラリソースにLGPL-2.1-or-laterとLGPL-2.0-or-laterを確認しています。CMakeのライセンスチェック引数だけで製品ライセンスを判定しないでください。

全推移依存・生成物・翻訳・同梱コードのライセンス監査は未完了です。配布前には告知、対応ソース提供、差替え可能性等を整備します。SASL機構ロード・KIMAP/KSMTP構築・合成サーバーTLSとSDK未導入guestでの開発用実行は確認済みですが、実サービス認証とRelease製品配布は引き続き検証対象です。
