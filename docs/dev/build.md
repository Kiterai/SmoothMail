# ビルド

## 準備・要件

- Windows x64、Visual Studio 2022のMSVC C++ x64ツールとWindows SDK。
- CMake 3.30以降、Git、Python 3.11以降をPATHへ登録。Pythonは配布・文書生成と依存ライブラリKI18n内で使用します。本体・依存のビルド入口はCMakeです。
- MSVCと一致する既存Qt 6 SDK（依存構築には6.10以降）。Core／Gui／Widgets／Sql／Network／Xml／Svg、テストにはTest、配置にはwindeployqtが必要です。確認済み環境はQt 6.12.0 `msvc2022_64`です。
- 固定ソースの取得先へのHTTPS接続と、依存の構築・保存に十分なディスク空き容量。

以下はリポジトリルートの **x64 Native Tools Command Prompt for VS 2022** で実行します。Qtのパスは環境に合わせて変更してください。他OSでの構築・動作は未検証です。

Ninja・vcpkgとQt以外の依存はスクリプトが取得・構築します。別のQtは取得せず、既存SDKも変更しません。取得元・版は `cmake/dependencies.json` で固定し、アーカイブのハッシュとTLS証明書を検証します。

### Windowsの制限付きサンドボックスでのHTTPS取得

CMakeのHTTPS取得がstatus 35で失敗する制限環境では、**OpenSSL対応curlの絶対パス**を明示指定してください（Git for Windows等の既存ツール）。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 "-DSMOOTHMAIL_DOWNLOAD_CURL=C:/path/to/Git/mingw64/bin/curl.exe" -P cmake/BuildWindows.cmake
```

指定時もHTTPS／TLS 1.2以上・証明書検証・固定ハッシュ照合を維持し、`.curlrc`は読みません。vcpkgへも設定を引き継ぎ、プロキシ変数は保持します。取得元・リダイレクト先への通信許可と構築先の書込み許可は別途必要です。更新済みのOpenSSL対応curlを使い、Schannel専用版は指定しないでください。未指定時はCMakeで取得し、失敗後の自動フォールバックは行いません。

## デバッグビルド

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_BUILD_TYPE=Debug -P cmake/BuildWindows.cmake
```

依存を `build/deps/` に準備し、アプリを `build/windows/Debug/smoothmail.exe` へ構築します。テストも構築しますが、自動実行はしません。

## リリースビルド

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_BUILD_TYPE=Release -P cmake/BuildWindows.cmake
```

アプリは `build/windows/Release/smoothmail.exe` です。DebugとReleaseの依存・成果物は分離されます。このEXEだけでは配布物になりません。[実行](run.md)・[配布](distribution.md)を参照してください。

## 再ビルド・別環境

アプリだけを再ビルドする場合：

```bat
cmake --build build/windows/Debug --parallel 4
cmake --build build/windows/Release --parallel 4
```

Qtやコンパイラを変更する場合は、新しい `-DDEPENDENCY_ROOT=C:/build/smoothmail-deps` と `-DAPPLICATION_BUILD=C:/build/smoothmail-app` を標準コマンドへ追加します。Windowsのパス長制限を避けるため短いパスを推奨します。既存ソース・取得物・パッチの不一致で停止した場合、強制上書きや検証の無効化はせず原因を確認してください。

提供済みのソースアーカイブを使う場合は、SmoothMailだけを新しいソースフォルダへ展開し、Releaseコマンドへ `-DSMOOTHMAIL_SOURCE_ARCHIVES=C:/source-archives -DSMOOTHMAIL_SOURCE_COMMIT=<提供されたcommit>` と新しい依存／アプリ構築先を追加します。依存を手動で `build/deps/src` へ展開したり、Git管理情報を捏造したりしません。固定されたアーカイブSHA256と全ファイルのtree digestを検証し、パッチは別コピーへ適用します。

外部ビルドツールの取得物を再利用するには `-DSMOOTHMAIL_ASSET_CACHE=C:/tool-downloads` を追加できます。工具・原本のハッシュ検証は維持します。Qt SDKの取得／変更は自動では行いません。Qt自体の対応ソース・ABI検証が必要な場合だけ、別の出力先で `cmake -DQt6_ROOT=... -DDEPENDENCY_ROOT=... -DSMOOTHMAIL_SOURCE_ARCHIVES=... -DQT_REBUILD_OUTPUT=... -P cmake/BuildQtSourceReview.cmake` を実行します。これは既存SDKの置換や配布承認ではありません。

キャッシュなしの全Release依存取得を検証する場合は、まだ存在しない専用rootで実行します（アプリ・ZIPの検証は含みません）。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DDEPENDENCY_ROOT=C:/build/smoothmail-clean-deps -P cmake/VerifyWindowsCleanDependencies.cmake
```
