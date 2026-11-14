# Windows配布物のライセンス・対応ソース監査

確認日: 2026-11-14。対象は`764a613`から収集したRelease候補39ファイル。**事前監査資料であり、配布許諾・最終監査の完了宣言ではありません。**

## 作成済みの資料

`cmake/PrepareWindowsLegalReview.cmake`で、実ファイルと対応する資料を新しい出力先へ収集します。

```bat
cmake -DSTAGE=build/integrated-privacy-runtime -DOUTPUT=build/legal-windows-review -P cmake/PrepareWindowsLegalReview.cmake
```

- `runtime-components-sha256.txt`: 39ファイルの相対名・SHA256・コンポーネント対応。
- `manifest.review.json`: 19コンポーネントの明示的なファイル対応、取得済み原文、ソース案内。
- `licenses/`: 原文のままのLICENSES/COPYING、公開ヘッダの著作権表示、KDEソース内の著作権表示の収集。
- `notices/`: コンポーネント別の固定ソース情報、既存のSmoothMailパッチ・経緯・機能制限説明。
- `THIRD_PARTY_NOTICES.md`と本資料。

Qt/CRTの不足を空欄のライセンス配列として残し、配布用`manifest.json`は生成しません。ファイルを改名するだけで配布してはいけません。Qt/CRT原文の不足は既存`--check-legal`でも拒否されます。出力先の上書き、未分類・重複分類・想定外のruntime構成は拒否します。生成物はGitや利用者の設定・資格情報を含みません。

## 構成と許諾条件の確認範囲

下記は原本・通知から確認した条件の種類です。KDEの各ファイル、Qt内蔵第三者コードを単一ライセンスへ一括認定するものではありません。原本LICENSES集合には、非同梱ツール等のライセンスも含まれます。

| コンポーネント | 同梱対象 | 版・ライセンス根拠／確認点 |
|---|---|---|
| SmoothMail | smoothmail.exe、qt.conf | MIT、リポジトリのLICENSE。EXEにはQt等のヘッダ/生成コードや静的組込みの確認も必要 |
| KCodecs | KF6Codecs.dll | 6.31.0。LGPL系等、固定ソースLICENSES。RFC 2047文字判定パッチあり |
| KConfig | KF6ConfigCore.dll | 6.31.0。LGPL系等。Windowsの外部設定移行無効化パッチあり |
| KCoreAddons | KF6CoreAddons.dll | 6.31.0。LGPL系、BSD、MPL等が原本に存在。採用コード別の条件確認が必要 |
| KCrash | KF6Crash.dll | 6.31.0。LGPL系等。Windowsの元ビルドprefix探索除去パッチあり |
| KI18n | KF6I18n.dll | 6.31.0。LGPL系等。Pythonはビルド時の間接利用で製品へ同梱しない |
| KIO | KF6KIOCore.dll | 6.31.0。LGPL系等。favicon実装除外・helper探索変更・Windows admin拒否。後述の追加監査が必要 |
| KMime | KF6Mime.dll | 6.31.0。LGPL系等。C文字判定のunsigned charパッチあり |
| KService | KF6Service.dll | 6.31.0。LGPL系等。原本LICENSESとソース著作権を収集 |
| KIMAP | KPim6IMAP.dll | 26.08.3（ライブラリ6.8.3）。LGPL系等。SASL継続応答・エラー通知パッチあり |
| KSMTP | KPim6SMTP.dll | 26.08.3。LGPL系等。EHLO能力リセットパッチあり |
| QtKeychain | qt6keychain.dll | 0.15.0。BSD-3-Clause系。COPYINGと公開keychain.hを収集。ソース変更なし |
| Cyrus SASL | libsasl.dll、sasl2/saslPLAIN.dll、sasl2/saslLOGIN.dll | 2.1.28。CMUの独自BSD系COPYING。著作権・条件・免責・謝辞を保持 |
| SASL XOAUTH2 | sasl2/saslXOAuth2.dll | 固定commit 36aabca…。MIT、Moriyoshi KoizumiのCOPYING。独自CMake統合で構築 |
| GNU libiconv | iconv-2.dll | 1.19。ライブラリはLGPL-2.1-or-later系、copyright原文を収集。GNUツールのGPL表記と区別 |
| GNU gettext libintl | intl-8.dll | 0.22.5。LGPL-2.1-or-later系、copyright原文を収集。gettextホストツールは同梱しない |
| Qt Base | Qt6Core/Gui/Network/Sql/Widgets/Xml.dll、platforms/qwindows.dll、sqldrivers/qsqlite.dll、tls/qschannelbackend.dll | 6.12.0。SDKのSBOMはLGPL/GPL/商用の代替条件等を記録。採用条件・内蔵第三者コードの通知は未確定 |
| Qt SVG | Qt6Svg.dll | 6.12.0。Qt SVGの原文・採用条件・対応ソースを要確認 |
| MSVC CRT | concrt140.dll、msvcp140.dll、msvcp140_1.dll、msvcp140_2.dll、msvcp140_atomic_wait.dll、msvcp140_codecvt_ids.dll、vccorlib140.dll、vcruntime140.dll、vcruntime140_1.dll、vcruntime140_threads.dll | MSVC再配布CRT。vcruntime140のFileVersionは14.44.35211.0。配布者の適用契約・REDIST対象の確認が必要 |

Cyrus SASLの必須謝辞:

> This product includes software developed by Computing Services at Carnegie Mellon University (http://www.cmu.edu/computing/).

対応表の全39ファイルは一意に分類します。PDB、ビルドツール、DrKonqi、kioexec、kioworker、adminプラグイン、gettext CLI、Flex/Bison、Python、vcpkgはこの候補に含まれません。**DLL名の非混入確認は、DLL内に静的組込みされたコードのライセンス監査を代替しません。**

## 固定ソースと変更の提供

- KDE各依存・QtKeychain・XOAUTH2の固定repository/commitは`cmake/dependencies.json`。7件の変更済みKDE依存は`patches/<dependency>/README.md`と対応パッチ、`THIRD_PARTY_NOTICES.md`を提供する。
- GNUはlibiconv 1.19 / gettext 0.22.5の原本、vcpkg固定commit `177e8175121536d414f405ed8b3099b325a42638`のport・既存パッチ、`cmake/native-triplets/x64-windows.cmake`と取得/構築スクリプトを対象にする。「SmoothMail独自GNUパッチなし」は無改変GNUという意味ではない。
- SASLは`BuildSaslDependencies.cmake`のtar.gzとSHA256、XOAUTH2は固定commitと`native-xoauth2`を対象にする。
- Qtの供給元SBOMで確認したソース識別子:
  - qtbase: `025bdad181de241e81bf853c8a2d7bf3d19261a9`
  - qtsvg: `883a58532078d003cefef0a549bfe9f885ce41f5`
  - SDKにあるSBOMの識別情報であり、現時点で完全な対応ソース一式を入手・照合したという意味ではない。通常の6.12.0タグだけで同一と推定しない。
- 構築説明は`docs/dev/dependencies.md`、`docs/dev/distribution.md`と`cmake/`以下。ライセンス原文・著作権・変更箇所/日付/機能差・必要な構築入力を対応ソースへ含める。

**原本URLとパッチへのリンクだけで、対応ソースの提供義務を満たしたとは扱いません。** 配布DLLに一致する完全な原本・変更・必要な生成入力/スクリプトを準備し、受領者が取得できる形で提供・検証する必要があります。ビルド者の`.git`、CMakeCache、個人パス入りreceiptをそのまま配布しないでください。

## 差替え・改変版利用

現候補はDLLによる動的リンクです。GPL/LGPL等を採用する箇所では、該当条項に従う改変・差替え・必要なデバッグの権利を製品の追加条件で禁止しないでください。

同一ABIの改変DLLで起動・必要機能が動くこと、署名/ハッシュ検査が利用者のライブラリ差替えを不当に制限しないことを、採用条項に沿って確認します。ビルド時の配布検査用ハッシュ固定は、アプリ実行時の差替え禁止ではありません。LGPLv3等でInstallation Informationが必要となる提供形態かも、最終提供方法に照らして判断します。

## 完了までに必要な確認・承認

| 項目 | 必要な対応 | 現状 |
|---|---|---|
| Qtの適用条件 | LGPL等のオープンソース条件で配布するか、配布権限のある商用契約を使うかを配布責任者が確認 | 未確認。個人の契約書やライセンスキーをGit/チャットへ出す必要はない |
| Qt原文・内蔵第三者通知 | SDKと一致する原本のLICENSES、著作権、qtbaseのSQLite/PCRE2/zlib/画像・フォント等、該当モジュールに実際に組み込まれたコードの条件を確認・収集 | SBOMは存在するが原文一式は未収集。SDK全体のSBOMには非同梱ツールもあるため全件を無条件に製品へ割り当てない |
| MSVC CRT配布権限 | 配布者に適用されるVisual StudioライセンスとREDIST一覧で10ファイル・対象環境の再配布条件を確認 | `VCToolsRedistDir`からの収集だけでは権限確認にならない |
| 対応ソース提供方法 | 同時配布アーカイブ、公開ダウンロード等、適用条項を満たす具体的な方法・提供先・保管責任者を決める | 提供場所未指定、完全ソースのアーカイブ/取得試験も未完了。未履行の書面オファーは作成しない |
| コード別条件と静的組込み | KDE/Qt/GNU等の実際のコンパイル/リンク対象とヘッダ/生成物/内蔵第三者コードを照合。特にKIOのGPL favicon除外だけで残り全部を認定しない | 構成・原文・変更説明は収集済み。最終判定は未完了 |
| 最終配布検証 | legal原文・ソース案内を確定後、manifest.jsonをレビューし、全同梱物の検査、独立ZIP展開・SHA照合、差替え・クリーン環境確認 | DIST-01/02に残る |

採用ライセンスの最終判断や契約上の再配布権限を、ソフトウェアの自動検査だけで認定することはできません。上記確認が終わるまではレビュー用資料として扱い、製品ZIPの公開・配布は行いません。
