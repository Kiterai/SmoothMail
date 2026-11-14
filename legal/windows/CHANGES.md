# 同梱ライブラリの変更通知

> レビュー用案。変更者はSmoothMail contributors。上流標準版と区別し、元の著作権・ライセンスを保持します。差分・詳しい説明は対応ソースの`patches/`にあります。

| 対象 | 変更日 | 内容・機能上の差 |
|---|---|---|
| KCodecs 6.31.0 | 2026-11-13 | `kcodecs.cpp`のRFC 2047解析で`isspace`へ渡すバイトをunsigned char化。Debug CRT assertionと未定義動作を修正 |
| KMime 6.31.0 | 2026-11-13 | `headerparsing.cpp`、`headers.cpp`、`parsers.cpp`、`util_p.cpp`のC文字判定・変換引数をunsigned char化。メール原文は書き換えない |
| KIMAP 26.08.3 | 2026-11-13 | `loginjob.cpp`のSASL継続応答で正当な空チャレンジを処理。不正応答時のエラー通知と接続解放を修正 |
| KSMTP 26.08.3 | 2026-11-13 | `session.cpp`でEHLO開始時に古い能力情報を破棄。STARTTLS前の広告をTLS後へ誤って持ち越さない |
| KConfig 6.31.0 | 2026-11-14 | Windowsで外部kconf_updateによる設定移行を無効化。`checkUpdate()`は警告のみで、外部起動・設定変更・移行済み記録を行わない |
| KCrash 6.31.0 | 2026-11-14 | WindowsのDrKonqi探索から元のビルドprefixを除去。環境変数・EXE隣・Qt実行時配置等の探索は維持 |
| KIO 6.31.0 | 2026-11-12、追加変更2026-11-14 | favicon cache実装を除外し`favIconForUrl()`は空文字列。Windows helper探索から元ビルドprefixを除去。未配置kioexecはエラー、Windows admin workerは拒否 |

KIOのGPL表示ファイルの除外は、それ以外のコードをMITへ変更するものではありません。対象ファイル固有のLGPL等を引き続き適用します。

GNU libiconv/libintlは固定vcpkgレシピの既存パッチとSmoothMailのWindows構築設定を使います。SmoothMailによる新しいGNUソースパッチは追加していませんが、「上流無改変のバイナリ」という意味ではありません。原本とともにvcpkgレシピ・既存パッチ・`cmake/native-triplets/x64-windows.cmake`をソース一式に含めます。

Cyrus SASLおよびXOAUTH2は対応ソース内のWindows構築設定を使用します。QtKeychainへSmoothMail独自のソースパッチは適用していません。Qtの通常の配置時にはwindeployqtによるprefix再配置を行う場合があります。Qtの供給元通知・署名等を作業者情報を消すために事後書換えする方式は採用しません。

再構築・差替え試験だけのQt SVG/KCodecs追加exportは製品の変更に含めません。上記にない変更を製品へ取り込む場合は、その変更通知と対応ソースを更新します。
