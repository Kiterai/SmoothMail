# WindowsポータブルZIP

## 前提

- Windows x64、指定Qtと一致するMSVC/Windows SDKのx64開発者コマンドプロンプト、CMake 3.30以降・Git・Python（KI18nからのみ利用）。
- 既存Qt SDKのRelease DLL／プラグイン。SDKの再インストールは不要です。
- Ninja・vcpkg・Qt以外の依存は内部取得・構築します。Debug/Releaseは分離し、変更済み依存のreceiptも生成・検証します。既存Qtは変更しません。
- MSVCの再配布可能なRelease CRTは`VCToolsRedistDir`から探索します。候補を一意に決定できなければ`-DCRT_BIN=.../x64/Microsoft.VC143.CRT`を指定します。Debug CRTは使用できません。
- 配布用のライセンス原文、著作権表示、対応ソースの入手方法・変更差分・ビルド方法などの通知をまとめたlegalディレクトリ。

## 1コマンドで生成

リポジトリで次を実行します。パスは準備済みの入力へ変更してください。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DLEGAL_DIR=C:/release-legal -DOUTPUT=dist/SmoothMail-windows-x64.zip -P cmake/PackageWindows.cmake
```

依存の取得・Release構築→アプリのReleaseビルド→必要DLL・プラグインとCRTの収集→ライセンス収集→個人パス・Debug混入の検査→ZIP化を行います。制御はCMake、PE/テキスト/ZIP検査と決定的圧縮は`tools/windows-package/`のC++補助ツールです。Python/curlを呼びません。`SmoothMail/`へ展開し、**smoothmail.exeを直接起動**します。開発用バッチ、PDB、DB、ログ、資格情報、CMakeキャッシュ、補助ツールは同梱しません。

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

同様にKDE各ライブラリ、QtKeychain、SASLと機構、libintl/libiconv、MSVC CRT等を追加してください。ファイルはmanifestからの相対パスで、空でないUTF-8テキストを用意します。参照した原文をそのままコピーし、著作権者の表示を削除しません。開発用receipt・ビルド記録は製品通知ではなく、そのまま同梱しません。

自動検査はDLLへの通知の対応付けを確認するもので、法的条件の充足を保証するものではありません。LGPLの対応ソース提供・改変通知・差替え可能性や、Microsoft CRT等の再配布権限は配布担当者が確認してください。

## プライバシーと検証

ZIPは固定タイムスタンプ・権限・相対名で作り、ユーザー名やホスト名を記録しません。アプリと依存のRelease構築ではソースパスをマッピングし、最後にASCII／両alignmentのUTF-16の絶対パスと入力ディレクトリ情報を検査します。既存Qtや依存DLLがビルドパスを保持していた場合も停止します。バイナリを書き換えて検査を回避することはしません。

オフライン検証: `tools/windows-package`をReleaseでconfigure/buildし、QtのbinをPATHへ追加して`ctest --test-dir <補助ツールのbuild> --output-on-failure`を実行します。合成PE・パス・legal・非上書き・決定的ZIPを検査し、CMake/libarchiveで独立にZIP展開・SHA256照合も行います。実Release ZIPの検証とは区別します。

ハッシュ一覧はZIP内の`runtime-sha256.txt`です。自動検査だけで任意の個人情報を完全に検出できるとは限らないため、実配布前には成果物・通知をレビューし、開発ツールのないWindowsで展開・直接起動・通信も確認してください。

この環境では統合処理によるDebug/Release依存とアプリの構築、Release DLL収集まで確認しました。ただし実ランタイム検査で`KF6ConfigCore.dll`の設定更新ツール用の絶対インストールパスを検出し、公開を停止しています。legal一式の監査、実Release ZIPの生成・クリーン環境での起動確認、キャッシュなしの全取得検証も未完了です。[未完了事項](../../spec/inprogress/distribution.md)を参照してください。Debugランタイムや合成ZIPを配布物の代用にはしていません。

DIST-04の設定調査では、通常のインストール先指定だけで現行DLLを再配置可能にする設定は見つかりませんでした。静的KConfigと未使用コード除去では最小EXEの検査を通せましたが、依存DLL間の共有状態やLGPL等の再リンク条件への影響があり、製品構成には採用していません。
