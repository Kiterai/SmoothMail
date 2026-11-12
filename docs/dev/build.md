# ビルドとテスト

現在はメイン画面の基盤と、解析済みデータを受け取る本文ビューを実装しています。本文ビューはHTMLをソース表示し、URL中の非ASCII文字を赤太字で表示します。メール通信・アカウント登録・送信・DB保存・添付ダウンロードは未実装です。未実装のメニューは無効表示です。

## 必要なもの

- CMake 3.25以降、Ninja、C++20対応コンパイラ。
- 開発者がインストールしたQt6 SDK（現時点の最小要求は6.8）。Core/Gui/Widgets、テスト時にはTest。
- WindowsではQt SDKと一致するMSVC x64開発環境。

依存は自動ダウンロードしません。KIMAP/KSMTP/KMime/QtKeychainの組込みは後続の実装で行います。現在のビルド成功は、これらの依存検証完了を意味しません。

## Windows

「x64 Native Tools Command Prompt for VS 2022」で実行します。QtとNinjaのパスは環境に合わせて変更してください。

```bat
set PATH=C:\Qt\Tools\Ninja;C:\Qt\6.12.0\msvc2022_64\bin;%PATH%
cmake --preset dev -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64
cmake --build --preset dev
ctest --preset dev
build\dev\smoothmail.exe
```

QtのbinをPATHに追加するのは開発時のDLL読込みのためです。配布物の依存収集・監査は別途必要です。

## macOS / Linux

```sh
cmake --preset dev -DQt6_ROOT=/path/to/Qt/6.x/platform
cmake --build --preset dev
ctest --preset dev
```

他OSでの動作はまだ検証していません。個人用設定はGit対象外の`CMakeUserPresets.json`に保存できます。別のQt SDKへ変更するときは、新しいビルドディレクトリを使ってください。

## 現在の検証状況

- Qt 6.12.0 / MSVC 2022で、メイン画面・本文ビューのビルドとCTestの全2ターゲット成功を確認済み。
- HTMLソース表示、URL装飾、危険なリンクの拒否、リソース読込み拒否、CRLF、ヘッダの開閉を自動テストしています。トレイの実操作やメールサーバ接続はまだ検証していません。
