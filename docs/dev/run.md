# 実行

[ビルド](build.md)後、リポジトリルートから実行します。独自の`-B`を使った場合はビルドディレクトリ部分を読み替えてください。DLL・プラグイン・CRTはビルド時に配置済みで、展開やPATH設定は不要です。起動時に実行ファイル隣の`sasl2`を使用します。

Debugの`run/`は開発・検証専用です。再配布できないDebug CRTを含むため第三者へ配布しないでください。デバッガには`run/smoothmail.exe`と`app/`の対応PDBを指定します。

Releaseの`run/`は生成ZIPから自動展開・全ファイル照合したものです。`run/runtime/smoothmail.exe`も直接起動でき、配布ZIPと同一内容です。実行用出力へ設定・DB・ログを追加して配布しないでください。

## デバッグ実行

```bat
build\dbg\run\launch.cmd
```

## リリースビルド実行

```bat
build\rel\run\launch.cmd
```
