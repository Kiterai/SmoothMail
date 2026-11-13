# WindowsポータブルZIP

## 前提（配布担当者が手動で準備）

- Windows x64、指定Qtと一致するMSVC x64開発者コマンドプロンプト、CMake・Ninja・Python 3.11以降。
- 既存Qt SDKのRelease DLL／プラグイン。SDKの再インストールは不要です。
- Qtと同じABIの**Release**依存prefix、libintl/libiconvのRelease bin、MSVCの再配布可能なRelease CRT bin。Debug DLLやDebug CRTは使用できません。
- 変更済み依存のreceiptを含むprefix。[依存構築](dependencies.md)にあるRelease手順・限定パッチの条件を満たす必要があります。パッケージ処理は依存の取得・ビルドを行いません。
- 配布用のライセンス原文、著作権表示、対応ソースの入手方法・変更差分・ビルド方法などの通知をまとめたlegalディレクトリ。

## 1コマンドで生成

リポジトリで次を実行します。パスは準備済みの入力へ変更してください。

```bat
python tools/package-windows.py --qt-root C:/Qt/6.12.0/msvc2022_64 --dependency-prefix C:/deps/Release --native-bin C:/deps/native/bin --crt-bin C:/redist/x64/Microsoft.VC143.CRT --legal-dir C:/release-legal --output dist/SmoothMail-windows-x64.zip
```

アプリのReleaseビルド→必要DLL・プラグインとCRTの収集→ライセンス収集→個人パス・Debug混入の検査→ZIP化を行います。`SmoothMail/`へ展開し、**smoothmail.exeを直接起動**します。開発用バッチ、PDB、DB、ログ、資格情報、CMakeキャッシュは同梱しません。

新規の専用ビルドディレクトリを使い、既存ビルドやZIPは上書きしません。SDKは変更せず、Qtの再配置用処理はステージ先のコピーだけへ適用します。未解決／競合DLL、指定外ディレクトリからのDLL解決、Debug混入、ライセンス不足、埋め込まれた絶対パスを検出すると公開せず停止します。

## legal/manifest.json

以下は構造の例です。実際のDLLすべてを対応するコンポーネントへ割り当てます。無関係なライセンスを一括指定するものではありません。

```json
{
  "schema": 1,
  "components": [
    {
      "name": "Qt",
      "version": "6.12.0",
      "binaries": ["Qt6*.dll", "platforms/*.dll", "sqldrivers/*.dll", "tls/*.dll"],
      "license_files": ["qt/licenses.txt", "qt/copyright.txt"],
      "source_notice": "qt/source-and-replacement.md"
    }
  ]
}
```

同様にKDE各ライブラリ、QtKeychain、SASLと機構、libintl/libiconv、MSVC CRT等を追加してください。ファイルはmanifestからの相対パスで、空でないUTF-8テキストを用意します。参照した原文をそのままコピーし、著作権者の表示を削除しません。開発用receiptはビルド者のローカルパスを含むため、そのまま同梱しません。

自動検査はDLLへの通知の対応付けを確認するもので、法的条件の充足を保証するものではありません。LGPLの対応ソース提供・改変通知・差替え可能性や、Microsoft CRT等の再配布権限は配布担当者が確認してください。

## プライバシーと検証

ZIPは固定タイムスタンプ・相対名で作り、ユーザー名やホスト名を記録しません。アプリのコンパイルではソースパスをマッピングし、最後にASCII／UTF-16の絶対パスと入力ディレクトリ情報を検査します。依存DLLがビルドパスを保持していた場合も停止します。必要に応じて依存をパスマッピング等を使って手動で再構築してください。バイナリを闇雲に書き換えて検査を回避することはしません。

ハッシュ一覧はZIP内の`runtime-sha256.txt`です。自動検査だけで任意の個人情報を完全に検出できるとは限らないため、実配布前には成果物・通知をレビューし、開発ツールのないWindowsで展開・直接起動・通信も確認してください。

現在のこの開発環境にはRelease依存prefixがないため、実Release ZIPの生成・クリーン環境でのRelease起動確認は未完了です。Debugランタイムを配布物の代用にはしていません。
