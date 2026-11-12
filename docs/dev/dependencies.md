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

KI18nのWindows構築にはlibintl、KIOにはKConfig/CoreAddons/I18nに加えてKService・Solid・KCrashが必要です。`KIOCORE_ONLY=ON`でも上流configureはKService/Solidを要求します。Solidにはビルド時のFlex/Bisonも必要です。ソースとコミットは確認済みですが、これらの残る構築はまだ完了していません。

### 認証方式の切替に注意

KSMTP 26.08.3の`LoginJobPrivate::selectAuthentication()`は、優先認証方式が広告されていない場合にLOGIN/PLAINを選ぶ実装です。アプリ側ではログインジョブの開始前に、サーバーが設定された方式を広告しているか確認する必要があります。特にOAuthトークンを別方式へ自動送信してはいけません。構築後、XOAUTH2を広告しないローカルサーバーへAUTHやトークンを送らないことを回帰テストします。

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

KMimeのライブラリソースにLGPL-2.0-or-later、KSMTPのライブラリソースにLGPL-2.1-or-laterとLGPL-2.0-or-laterを確認しています。CMakeのライセンスチェック引数だけで製品ライセンスを判定しないでください。

全推移依存・生成物・翻訳・同梱コードのライセンス監査は未完了です。配布前には告知、対応ソース提供、差替え可能性等を整備します。KIMAP/KSMTP/SASLのビルド・XOAUTH2プラグイン・TLS・実サービス認証も引き続き検証対象です。
