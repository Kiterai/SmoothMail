# ビルド

## 準備・要件

- Windows x64、Visual Studio 2022のMSVC C++ x64ツールとWindows SDK。
- CMake 3.30以降、Git、Python 3.11以降をPATHへ登録。
- MSVCと一致する既存Qt 6 SDK（依存構築には6.10以降）。Core／Gui／Widgets／Sql／Network／Xml／Svg、DebugにはTestとDebug DLL、配置にはwindeployqtが必要です。確認済み環境はQt 6.12.0 `msvc2022_64`です。
- 固定ソース取得先へのHTTPS接続、依存の構築・保存に十分な空き容量。

リポジトリルートの **x64 Native Tools Command Prompt for VS 2022** で実行します。他OSは未検証です。Ninja・vcpkgとQt以外の依存は自動取得・構築し、既存Qt SDKは変更しません。取得元・版・ハッシュは`cmake/dependencies.json`等で固定し、TLS証明書と原本の検証を維持します。

`.env.example`を参考に、UTF-8の`.env`を作成してください。`SMOOTHMAIL_QT_ROOT`にQt SDKのパスを指定し、名称・版・配布URLを設定します。Releaseでは実際のHTTPS配布URLが必要です。全項目は[配布手順](distribution.md#envの設定)を参照してください。パスは空白を含められますが、区切りには`/`を使い、変数展開は使いません。

標準の依存保存先は`build/deps`です。Qt・コンパイラを変更する場合は、`.env`の`SMOOTHMAIL_DEPENDENCY_ROOT`と下記`-B`に新しい短いパスを指定してください。制限環境でCMakeのHTTPS取得がstatus 35になる場合は、`SMOOTHMAIL_DOWNLOAD_CURL`へGit for Windows等のOpenSSL対応curlの絶対パスを設定します。TLS／証明書／ハッシュ検証を無効化せず、Schannel専用版は指定しません。

`-B`と`--build`の引数で同じビルドディレクトリを明示します。Debug／Releaseは別ディレクトリにします。構築中のアプリは終了してください。再ビルドでも同じ2行を使えます。実行環境を毎回新しく生成し、前回の成功出力は`run-previous-*`／`artifacts-previous-*`として残します。中間結果は`jobs/`以下へ保持します。不要な旧出力の整理は作業者が行ってください。

Debugはテストも構築しますが自動実行しません。Releaseは配布時と同じプライバシー対策・通知埋込みを行い、実行環境に加えZIP・対応ソース・ハッシュ一覧も生成します。ソースとの一致のため、Release構築前に追跡ファイルの変更をコミットしてください。生成成功は動作確認・法的監査の完了を意味しません。

成果物は`<ビルドディレクトリ>/run/`、本体の内部構築先は`app/`です。[実行](run.md)・[テスト](test.md)・[配布](distribution.md)を参照してください。

提供済み対応ソースから本体を再構築する場合は、SmoothMailを新しいフォルダへ展開し、従来の低水準入口`cmake/BuildWindows.cmake`へ`Qt6_ROOT`、`DEPENDENCY_ROOT`、`APPLICATION_BUILD`、`CMAKE_BUILD_TYPE=Release`、`SMOOTHMAIL_SOURCE_ARCHIVES`、`SMOOTHMAIL_SOURCE_COMMIT`を`-D`で渡します。原本アーカイブのSHA256と展開tree digestを検証し、パッチは別コピーへ適用します。配布版の通知も埋め込む場合は`SMOOTHMAIL_DISTRIBUTION=ON`と`SMOOTHMAIL_LEGAL_DIR=<展開した配布版のlicenses>`を追加します。Git管理情報を捏造したり、既存SDKや原本を書き換えたりしません。この再構築用入口は新規配布物作成の入口ではありません。

## デバッグビルド

```bat
cmake --preset debug -B build/dbg
cmake --build build/dbg
```

## リリースビルド

```bat
cmake --preset release -B build/rel
cmake --build build/rel
```
