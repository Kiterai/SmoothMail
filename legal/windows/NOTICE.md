# SmoothMail — 利用許諾・第三者ソフトウェア通知

> レビュー用案。公開前に同梱構成・公開先・追加条件の同意方法を確定してください。

## SmoothMail本体

Copyright (c) 2026 SmoothMail contributors

SmoothMail独自コードはMIT Licenseで提供します。使用、複製、改変、再配布等の許可と条件、保証の否認・責任の制限については、同梱の`licenses/SmoothMail/LICENSE`を参照してください。

このMIT Licenseは、以下の第三者ソフトウェアをMITへ再ライセンスするものではありません。各コンポーネントと内蔵第三者コードには、それぞれの著作権表示・ライセンス原文・通知が適用されます。

## 同梱コンポーネント

| コンポーネント | 版 | 適用条件の案内 |
|---|---|---|
| Qt Base / Qt SVG | 6.12.0 | 対象QtモジュールはLGPL-3.0-onlyのオープンソース条件を採用。内蔵第三者コードの固有条件も適用 |
| KCodecs / KConfig / KCoreAddons / KCrash / KI18n / KIO / KMime / KService | 6.31.0 | LGPL系ほか、各ファイルに記載された条件。詳細はコンポーネント通知・原文・著作権表示 |
| KIMAP / KSMTP | 26.08.3 | LGPL系ほか各ファイルの条件。KIMAPのライブラリ版は6.8.3 |
| QtKeychain | 0.15.0 | BSD 3-Clause。上流COPYINGおよび公開ヘッダの著作権表示 |
| Cyrus SASL | 2.1.28 | Carnegie Mellon UniversityのCOPYINGに記載された条件 |
| Cyrus SASL XOAUTH2プラグイン | 固定commit（ソース一覧参照） | MIT。上流COPYING |
| GNU libiconv | 1.19 | ライブラリのLGPL-2.1-or-later等。原文・個別コードの通知を参照 |
| GNU gettext libintl | 0.22.5 | ライブラリのLGPL-2.1-or-later等。原文・個別コードの通知を参照 |
| Microsoft Visual C++ Release CRT | v14 | Microsoftの条件。[CRTに限る追加条件](MICROSOFT-RUNTIME.md)を参照 |

実際のDLL／プラグインと原文の対応は、同梱の`COMPONENTS.md`およびコンポーネント別`notices/`に記載します。上流リポジトリ全体から収集した原文には、非同梱コードや代替条件もあります。原文の収録だけで製品全体のライセンスを変更するものではありません。

## Qtと内蔵第三者コード

Qtの商用ライセンスではなく、対象モジュールのLGPL-3.0-onlyの選択肢を使用します。LGPL v3およびそれが参照するGPL v3の原文は`licenses/qtbase/LICENSES/`と`licenses/qtsvg/LICENSES/`に含みます。これによりSmoothMail独自コードやMicrosoft CRTをGPLへ再ライセンスするものではありません。

Qtに含まれるFreeType、HarfBuzz、libpng、PCRE2、zlib、Unicodeデータ、Public Suffix List、SQLiteその他の第三者コードは、それぞれの条件を保持します。原文・著作権・attributionは`licenses/qtbase/`、`licenses/qtsvg/`を参照してください。FreeTypeおよび対応するrasterizerの代替条件はFTLを選択します。

> Portions of this software are copyright (C) 2026 The FreeType Project (www.freetype.org). All rights reserved.

依存関係・著作権・条件の確認用一覧を添付する場合、その一覧は原文の代わりにはなりません。`NOASSERTION`は権利放棄や無条件の許諾を意味しません。

## Cyrus SASLの謝辞

Copyright (c) 1998-2003 Carnegie Mellon University. All rights reserved.

> This product includes software developed by Computing Services at Carnegie Mellon University (http://www.cmu.edu/computing/).

著作権、全条件、免責は`licenses/cyrus-sasl/COPYING`に収録します。Carnegie Mellon Universityやその他の権利者がSmoothMailを推奨・保証していることを示すものではありません。

## 改変・差替え・ソース

変更済み依存の変更者・変更日・内容は[CHANGES.md](CHANGES.md)、ソースの入手と再構築・差替え方法は[SOURCE-AND-REPLACEMENT.md](SOURCE-AND-REPLACEMENT.md)を参照してください。

SmoothMailは、適用されるオープンソースライセンスによる改変・再配布・再リンク・差替え、およびその改変のデバッグに必要なリバースエンジニアリングを追加条件で禁止しません。配布作成時のハッシュ検査は、利用者の実行時に改変ライブラリを禁止するものではありません。

## 保証・商標

各コードに適用される原文の保証否認・責任制限に従います。法律上排除できない権利をこの案内によって制限しません。権利者の名称・商標に関する権利は各権利者に帰属し、その名称の記載は推薦・提携・保証を意味しません。
