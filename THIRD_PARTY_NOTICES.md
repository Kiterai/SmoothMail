# 第三者コードと暫定変更に関する通知

SmoothMail独自コードのMITライセンスは、Qt・KDEその他の第三者コードをMITへ再ライセンスするものではありません。各コードの元の著作権表示・ライセンスが引き続き適用されます。

**このファイルは開発中の通知です。全依存を網羅した配布用ライセンス一覧・対応ソース提供手続きの整備は未完了です。**

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

## その他の依存

Qt、KMime、KIMAP、KSMTP、KDE Frameworks、Cyrus SASL、XOAUTH2プラグイン、GNU libintl/libiconv等は、それぞれの許諾条件に従います。バージョン・取得元は`cmake/dependencies.json`と`docs/dev/dependencies.md`、個別のライセンスは取得元ソースおよび各prefixのライセンス情報を参照してください。

gettext/Flex/Bison等のビルド専用ツールと、アプリへリンクするライブラリは区別します。これらのツールを製品へ同梱する場合は、別途その配布条件を確認する必要があります。
