# 第三者コードと暫定変更に関する通知

SmoothMail独自コードのMITライセンスは、Qt・KDEその他の第三者コードをMITへ再ライセンスするものではありません。各コードの元の著作権表示・ライセンスが引き続き適用されます。

**このファイルだけでは全依存の配布監査完了を意味しません。配布ZIPでは、実際に同梱する全DLLに対応するライセンス原文・著作権・対応ソースの通知を別途`legal/manifest.json`へまとめます。準備手順は`docs/dev/distribution.md`を参照してください。**

## KIO 6.31.0：favicon機能を無効化した暫定ビルド

- 上流および著作権者: KDE KIO contributors。各ソースファイルの著作権表示を参照。
- 取得元: https://invent.kde.org/frameworks/kio
- 固定コミット: `2dd4bdc916a6f46d297d8788f5b628181483315d`（v6.31.0）。
- SmoothMailによる変更日: 2026-11-12。
- 変更差分・説明: `patches/kio/6.31.0-disable-favicon-cache.patch`、`patches/kio/README.md`。

上流の`src/core/faviconscache.cpp`と`faviconscache_p.h`は、`GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL`と表示されています。SmoothMail向けのKIOCoreビルドでは、その実装ファイルのコンパイルと対応privateヘッダの利用を除外します。

LGPL-2.0-onlyと表示された`src/core/global.cpp`を変更し、`KIO::favIconForUrl()`は常に空文字列を返します。公開関数は残りますが、faviconキャッシュの参照・検索はできません。これらの変更は上流KIOの標準動作ではなく、SmoothMail向けの暫定措置です。元のライセンス・著作権表示は保持し、GPL実装をコピーして代替コードにすることはありません。

KIOはファイルごとに許諾条件が異なります。対象2ファイルの除外だけで、残るコード・生成物・翻訳・推移依存がすべてMITで配布可能になったとは主張しません。LGPLの告知・対応ソース・ライブラリ差替え可能性等の義務も残ります。

ビルド手順は、インストール先の`share/licenses/kio`へ上流の`LICENSES`ディレクトリ、変更パッチ、変更通知を配置します。配布時には、採用DLLに対応した原本ソース・パッチ・ビルド手順を提供できるよう整備する必要があります。開発用prefix全体を、そのまま製品の配布物として扱ってはいけません。

## KIMAP 26.08.3：SASL継続応答とエラー通知の修正

- 上流: https://invent.kde.org/pim/kimap
- 固定コミット: `fe0df283d6745c5ad1e0478db0a74ea0a2205d5a`（v26.08.3、ライブラリ版6.8.3）。
- 変更日・変更者: 2026-11-13、SmoothMail contributors。
- 差分と説明: `patches/kimap/26.08.3-fix-sasl-continuation.patch`、`patches/kimap/README.md`。

`src/loginjob.cpp`を変更し、PLAIN以外でも正当な空のSASLチャレンジを処理します。また、不正応答時にはエラーコードを設定して認証中のSASL接続を解放します。認証方式の自動切替やTLS検証の緩和は追加しません。

対象ファイルのLGPL-2.0-or-laterおよび元の著作権表示を保持します。この修正は上流の標準実装ではありません。ビルド時に`share/licenses/kimap`へ上流ライセンス・パッチ・変更通知を配置します。配布時の対応ソース提供等の義務は引き続き残ります。

## その他の依存

### KCodecs 6.31.0のRFC 2047文字判定修正

上流 https://invent.kde.org/frameworks/kcodecs の固定コミット`220095a518c36c1b7aa6b5f49996177617979031`へ、`patches/kcodecs/6.31.0-safe-ctype.patch`を適用します。2026-11-13、SmoothMail contributorsによる`src/kcodecs.cpp`の局所変更です。`decodeRFC2047String`で`isspace`へ渡す入力バイトをunsigned charへ変換し、Debug CRTアサートと未定義動作を修正します。元の著作権・LGPL-2.0-or-laterを保持し、上流LICENSES・差分・変更通知・receiptをprefixの`share/licenses/kcodecs`へ配置します。上流リリースの標準実装ではなく、対応ソース提供・ライブラリ差替え等の義務は引き続き残ります。

### KMime 6.31.0のC文字判定引数の修正

上流 https://invent.kde.org/frameworks/kmime の固定コミット`d28bed05be65e7f1234bb852ad81a67b13df53ea`に、`patches/kmime/6.31.0-safe-ctype.patch`を適用します。2026-11-13にSmoothMail contributorsが`src/headerparsing.cpp`、`headers.cpp`、`parsers.cpp`、`util_p.cpp`を変更し、非ASCIIの入力バイトを`isdigit`/`isspace`/`toupper`へ渡す際に`unsigned char`へ変換します。MSVC Debug CRTのアサートと、他ランタイムでの未定義動作を避けるための局所変更です。アサートの無効化や不正なメール原文の書換えはしません。対象ファイルのLGPL-2.0-or-laterおよび元の著作権表示を保持します。上流の標準実装ではなく、上流LICENSES・差分・変更通知・ビルド検証用receiptをprefixの`share/licenses/kmime`へ配置します。対応ソース提供等の配布義務は引き続き残ります。

### KSMTP 26.08.3の能力情報リセット

上流 https://invent.kde.org/pim/ksmtp の固定コミット`f78c9bbcf690f5f829ccaf31921caec650579f20`に、`patches/ksmtp/26.08.3-reset-ehlo-capabilities.patch`を適用します。2026-11-13にSmoothMail contributorsが`src/session.cpp`を変更し、EHLO握手開始時に古いAUTH/SIZE/STARTTLS/DSN/SMTPUTF8広告を破棄します。これにより、STARTTLS前の認証方式をTLS後の認証前チェックで誤って許可することを防ぎます。元のLGPL-2.1-or-laterおよび著作権表示は保持します。上流未採用の変更であり、差分・上流LICENSES・変更通知をprefixの`share/licenses/ksmtp`へ配置します。対応ソース提供等の配布義務は残ります。

QtKeychain 0.15.0（`ad7344c45a86a4f66cbafc4b081b5f7b876cb0b7`、https://github.com/frankosterfeld/qtkeychain）は変更せず指定Qt SDKで構築します。Frank Osterfeldほかの著作権表示と上流`COPYING`（BSD系の3条件）に従います。`share/licenses/qtkeychain`へCOPYINGと著作権表示を含む公開ヘッダを配置します。

Qt、KMime、KIMAP、KSMTP、KDE Frameworks、Cyrus SASL、XOAUTH2プラグイン、GNU libintl/libiconv等は、それぞれの許諾条件に従います。バージョン・取得元は`cmake/dependencies.json`と`docs/dev/dependencies.md`、個別のライセンスは取得元ソースおよび各prefixのライセンス情報を参照してください。

gettext/Flex/Bison等のビルド専用ツールと、アプリへリンクするライブラリは区別します。これらのツールを製品へ同梱する場合は、別途その配布条件を確認する必要があります。
