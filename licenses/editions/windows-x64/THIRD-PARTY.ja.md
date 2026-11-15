# @SOFTWARE_NAME@ — 第三者ソフトウェア

**@NOTICE_STATUS_JA@[英語正本](THIRD-PARTY.md)の日本語訳。相違がある場合は英語正本を優先する。第三者ライセンスは各原文に従う。**

## 第三者ソフトウェア

第三者ソフトウェアには、それぞれの著作権表示及びライセンスを適用する。当アプリケーションのMIT Licenseは、当該ソフトウェアを再許諾するものではない。

| コンポーネント | 版 | ライセンス通知 |
|---|---|---|
| Qt Base / Qt SVG | 6.12.0 | QtモジュールにはLGPL-3.0-onlyの選択肢を採用。内蔵第三者コードには固有のライセンスを適用 |
| KCodecs / KConfig / KCoreAddons / KCrash / KI18n / KIO / KMime / KService | 6.31.0 | 付属通知及び原文に示すLGPLその他のファイル別適用条件 |
| KIMAP / KSMTP | 26.08.3 | LGPLその他のファイル別適用条件 |
| QtKeychain | 0.15.0 | BSD-3-Clause |
| Cyrus SASL | 2.1.28 | `COPYING`記載のCarnegie Mellon Universityライセンス |
| Cyrus SASL XOAUTH2 | コンポーネント別通知記載のソース改訂 | MIT |
| GNU libiconv | 1.19 | LGPL-2.1-or-later及びファイル別適用通知 |
| GNU gettext libintl | 0.22.5 | LGPL-2.1-or-later及びファイル別適用通知 |
| Microsoft Visual C++ランタイム | v14 | [Microsoftランタイム適用条件](MICROSOFT-RUNTIME.ja.md) |

コンポーネント別通知に配布対象ファイルを特定し、ライセンス全文及び著作権表示を収録又は参照する。代替ライセンス又は他の上流ファイルに関する原文の収録のみをもって、当該条件をアプリケーション全体に適用するものではない。

## Qt及び内蔵第三者コード

QtはGNU Lesser General Public License第3版に基づき使用する。LGPL v3及び同ライセンスが参照するGNU GPL v3の原文は、`originals/qtbase/LICENSES/`及び`originals/qtsvg/LICENSES/`に収録する。

LGPL v3第4節は、条件を総合した結果、次の行為を実質的に制限しない条件下で結合著作物を提供することを認める。

> 結合著作物に含まれるライブラリ部分の改変、及び当該改変のデバッグを目的とするリバースエンジニアリング

@SOFTWARE_NAME@は当該行為に追加的制限を課さない。対応ソース及び差替えに関する情報は[SOURCE-AND-REPLACEMENT.ja.md](SOURCE-AND-REPLACEMENT.ja.md)に記載する。

Qt内蔵第三者コードの著作権表示及びライセンス原文は、`originals/qtbase/`及び`originals/qtsvg/`に収録する。FreeType及び対象rasterizerコードには、FreeType License（FTL）の選択肢を採用する。必須謝辞は次のとおりである。

> 当ソフトウェアの一部の著作権は、2026年The FreeType Project（www.freetype.org）に帰属する。全ての権利を留保する。

## Cyrus SASL

Copyright (c) 1998-2003 Carnegie Mellon University. All rights reserved.

> 当製品には、Carnegie Mellon UniversityのComputing Services（http://www.cmu.edu/computing/）が開発したソフトウェアを含む。

条件及び免責の全文は`originals/cyrus-sasl/COPYING`に収録する。

## 改変コンポーネント

改変通知は[CHANGES.ja.md](CHANGES.ja.md)に記載する。原著作権表示及びライセンス条件を保持する。

## ライブラリ差替え

当Windowsポータブル配布では、アプリケーションのディレクトリ及び対象プラグイン用サブディレクトリに差替えライブラリを配置する。アプリケーション固有の認可鍵は不要である。
