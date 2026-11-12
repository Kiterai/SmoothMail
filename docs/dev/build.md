# ビルドとテスト

現在はメイン画面、本文ビュー、KMimeから表示モデルへの解析層を実装しています。本文ビューはHTMLをソース表示し、URL中の非ASCII文字を赤太字で表示します。解析層は単体テスト段階で、画面へのメール読込みにはまだ接続していません。メール通信・アカウント登録・送信・DB保存・添付ダウンロードは未実装です。未実装のメニューは無効表示です。

## 必要なもの

- CMake 3.25以降、Ninja、C++20対応コンパイラ。
- 開発者がインストールしたQt6 SDK（現時点の最小要求は6.8）。Core/Gui/Widgets、テスト時にはTest。
- WindowsではQt SDKと一致するMSVC x64開発環境。
- 通常ビルドではKMime 6.31以降。[依存構築](dependencies.md)で先に構築してください。この手順にはCMake 3.30以降・Git・Qt 6.10以降が必要です。

アプリのconfigure時には依存を自動ダウンロードしません。KIMAP/KSMTP/QtKeychainは後続の実装で組み込みます。KMime以外の依存検証完了を意味しません。

## Windows

「x64 Native Tools Command Prompt for VS 2022」で実行します。QtとNinjaのパスは環境に合わせて変更してください。

```bat
set PATH=C:\Qt\Tools\Ninja;C:\Qt\6.12.0\msvc2022_64\bin;%CD%\build\deps\install\Debug\bin;%PATH%
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P cmake/BuildMimeDependencies.cmake
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64
cmake --build --preset dev
ctest --preset dev
build\dev\smoothmail.exe
```

Qtおよび依存prefixのbinをPATHに追加するのは開発時のDLL読込みのためです。配布物の依存収集・監査は別途必要です。

UIのみを開発する場合は`-DSMOOTHMAIL_BUILD_MIME=OFF`でKMimeを使わずビルドできます。これはメール解析の代替実装ではありません。

## macOS / Linux

```sh
cmake -DQt6_ROOT=/path/to/Qt/6.x/platform -P cmake/BuildMimeDependencies.cmake
cmake --preset dev -DQt6_ROOT=/path/to/Qt/6.x/platform
cmake --build --preset dev
ctest --preset dev
```

他OSでの動作はまだ検証していません。個人用設定はGit対象外の`CMakeUserPresets.json`に保存できます。別のQt SDKへ変更するときは、新しいビルドディレクトリを使ってください。

## 現在の検証状況

- Qt 6.12.0 / MSVC 2022で、メイン画面・本文ビュー・KMime解析層のビルドとCTestを確認しています。
- HTMLソース表示、URL装飾、危険なリンクの拒否、リソース読込み拒否、CRLF、ヘッダの開閉を自動テストしています。トレイの実操作やメールサーバ接続はまだ検証していません。
- MIME解析ではUTF-8、ISO-2022-JP、Shift_JIS、CP932、RFC 2047件名、RFC 2231添付名、multipart、Base64/Quoted-Printableをテストしています。採用タグ全体の互換性を保証するものではありません。
