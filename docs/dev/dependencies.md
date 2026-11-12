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

26.08.3のソースと実コミットを確認し、manifestに記録しました。**上記のMIME専用bootstrapではビルドしません。**

上流CMakeで以下の直接依存を確認しています。

| 対象 | 主な直接依存 |
|---|---|
| KIMAP | Qt Network、KMime、CoreAddons、I18n、KIOCore、Cyrus SASL |
| KSMTP | Qt Network、CoreAddons、I18n、KIOCore、Cyrus SASL |
| KMime | Qt Core、KCodecs |

KIMAP/KSMTPの要求はQt 6.9以降、Frameworks 6.25以降、CMake 3.29以降です。ただし今回選んだKMime/KCodecs 6.31.0はQt 6.10以降・CMake 3.30以降が必要です。

## ライセンスと残る検証

KMimeのライブラリソースにLGPL-2.0-or-later、KSMTPのライブラリソースにLGPL-2.1-or-laterとLGPL-2.0-or-laterを確認しています。CMakeのライセンスチェック引数だけで製品ライセンスを判定しないでください。

全推移依存・生成物・翻訳・同梱コードのライセンス監査は未完了です。配布前には告知、対応ソース提供、差替え可能性等を整備します。KIMAP/KSMTP/SASLのビルド・XOAUTH2プラグイン・TLS・実サービス認証も引き続き検証対象です。
