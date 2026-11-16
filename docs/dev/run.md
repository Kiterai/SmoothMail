# 実行

[ビルド](build.md)後、リポジトリルートのMSVC x64開発者コマンドプロンプトで実行します。Qtや依存rootを変えた場合は以下のパスを読み替えてください。

## デバッグ実行

必要なDLL・プラグインとDebug CRTを、新しい実行用フォルダへ集めます。

```bat
cmake -DAPP="%CD%/build/windows/Debug/smoothmail.exe" -DQT_BIN=C:/Qt/6.12.0/msvc2022_64/bin -DDEPENDENCY_BIN="%CD%/build/deps/install/Debug/bin" -DNATIVE_BIN="%CD%/build/deps/native/gnu-runtime-v1/x64-windows/debug/bin" -DSASL_DIR="%CD%/build/deps/install/Debug/bin/sasl2" -DDESTINATION="%CD%/build/run-debug" -DCONFIGURATION=Debug -P cmake/StageWindowsRuntime.cmake
build\run-debug\start-smoothmail.cmd
```

配置先は空である必要があります。再配置時はアプリを終了し、新しい保存先を指定します。既存の起動環境を削除して作り直す必要はありません。デバッガを使う場合は同フォルダの `smoothmail.exe` と、対応するビルドのPDBを指定します。SASL探索先は実行ファイル隣の `sasl2` です。

この配置は開発・検証専用です。Debug CRTを含むため第三者へ配布しないでください。

## リリースビルド実行

Release CRTの場所は環境に合わせて変更してください。`VCToolsRedistDir` 配下の **x64／Microsoft.VC143.CRT** を使い、Debug用の `debug_nonredist` は使いません。

```bat
python tools/stage_windows_release.py --app build/windows/Release/smoothmail.exe --qt-bin C:/Qt/6.12.0/msvc2022_64/bin --dependency-bin build/deps/install/Release/bin --native-bin build/deps/native/gnu-runtime-v1/x64-windows/bin --sasl-dir build/deps/install/Release/bin/sasl2 --crt-bin "%VCToolsRedistDir%x64/Microsoft.VC143.CRT" --destination build/run-release
build\run-release\smoothmail.exe
```

こちらも新しい空の配置先を使います。必要なCRTだけを再帰的に選択し、署名・x64・版・非改変を確認するビルド専用C++補助をCMakeで内部構築します。依存rootを変更した場合は`--dependency-root`も指定し、提供原本から構築した環境では`--source-archives <アーカイブ置場>`も引き継いでください。必要なら`--asset-cache`／`--download-curl`を指定できます。配置だけでは配布用のライセンス・プライバシー検査は完了しません。実際に配布するZIPと、配布担当者の責務は[配布手順](distribution.md)を参照してください。
