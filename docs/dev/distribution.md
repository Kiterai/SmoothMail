# Windows版の配布

配布形式はWindows x64向けポータブルZIPです。展開した `SmoothMail/smoothmail.exe` を直接起動します。

**2026-11-16、利用者による動作・ライセンス内容／表示・再構築性の確認と配布物監査（DIST-02）、及び最終入力による標準入口の通し実行・独立ZIP検査（DIST-01）が完了しました。** 利用者確認の対象は[成果物記録](../../agent/license-display-final-zips.md)、標準入口のコマンド・修正・生成物SHA256は[通し検証記録](../../agent/dist01-standard-entry-verification.md)を参照してください。必要ファイルの正当な公開・配布とMSVC CRTの許諾表示は配布作業担当者の責務です。公開を実施したとの記録ではありません。

## 1. 入力を準備する

[ビルド要件](build.md)に加え、次を準備します。

- 配布するソースを確定した、追跡ファイルに変更のないGitコミット。
- Qt SDKのRelease DLL・プラグインと、MSVC 2022のx64 Release再配布用CRT。
- 全同梱コンポーネントの原文・著作権・変更通知・対応ソース案内をまとめた生成済みライセンス資料と、レビュー済みの `manifest.json`。

依存の構築後、以下で通知と対応ソースの**レビュー用資料**を収集できます。`STAGE` には [Release配置手順](run.md#リリースビルド実行)で作成した候補を指定し、各 `OUTPUT` は新しい保存先にします。

```bat
cmake -P cmake/FetchQtLegalSources.cmake
cmake -DSTAGE=build/run-release -DOUTPUT=build/legal-review -P cmake/PrepareWindowsLegalReview.cmake
cmake -DOUTPUT=build/source-review -P cmake/PrepareWindowsSourceReview.cmake
cmake -DREVIEW=build/source-review -P tests/cmake/verify_source_review.cmake
```

別の依存rootを使った場合は収集コマンドにも `-DDEPENDENCY_ROOT=...` を指定します。ソース収集はHEADの追跡ソースを使うため、配布対象の変更は事前にコミットしてください。

`manifest.review.json`・`source-manifest.review.json` は完成品の証明ではありません。原文と実際の組込みコード、全依存と変更差分、再構築・DLL差替え・最終バイナリとの対応を確認します。`review_only` 資料は改名しても配布入力として拒否されます。

### テンプレートと版別設定

共通の文面は[`licenses/common/`](../../licenses/common/)、Windows x64固有の文面は[`licenses/editions/windows-x64/`](../../licenses/editions/windows-x64/)で管理します。`filename.md`を英語正本、`filename.ja.md`を常体・漢語基調の日本語訳とします。これらは完成文書ではなく`@変数名@`形式のテンプレートです。第三者ライセンス原文と著作権者名は改名・翻訳・変数置換せず保持します。

リポジトリ直下の`.env.example`を参考に、Git管理外の`.env`へ次の3値を指定します。

| 変数 | 内容 |
|---|---|
| `SOFTWARE_NAME` | ライセンス文面に表示するソフトウェア名称 |
| `DISTRIBUTION_URL` | バイナリと対応ソースを提供するHTTPSページ |
| `BUILD_VERSION` | 対象ビルドの版情報 |

UTF-8の`KEY=value`形式です。値全体の単一／二重引用符を許容し、変数展開・シェル実行・行末コメントは行いません。空値・不足・重複・未知のキー、認証情報付きURL、Markdownを破壊する文字等は拒否します。診断には設定値を出力しません。既存のアプリ資格情報用`.env`を兼用せず、必要な場合は`-DENV_FILE=<専用ファイル>`で入力を分離してください。`.env`自体は成果物へ含めません。

`.env.example`のURLは例示用であり、実公開先ではありません。例を明示的に使って文面を確認する場合だけ`-DENV_FILE=.env.example`を指定します。既存の`.env`や過去の成果物は上書きしません。

原文収集結果とまとめるには次を実行します（出力は未承認案のままであり、製品生成ではありません）。

```bat
cmake -DEDITION=windows-x64 -DEVIDENCE=build/d05-legal-baseline-review -DSOURCE_REVIEW=build/d05-source-review -DQT_SBOM_REVIEW=build/qt-shipped-sbom-review.json -DOUTPUT=build/windows-licenses-review -P cmake/PrepareWindowsLegalDraft.cmake
```

生成物の`README.ja.md`を起点に文面を確認します。公開ページURL・版／コミット・最終構成との対応をレビューし、MSVC CRTに関するダウンロードページの表示は下記「ライセンス上の注意」に従い配布者が実施します。変数置換・原文掲載・ファイルの改名だけを承認・同意取得とは扱いません。

名称・URL・ビルド版又は版別テンプレートを変更した場合は、新しい出力先へ再生成し、その版・ビルドについて再レビューします。配布コマンドには、その生成物を確認・承認したディレクトリを渡します。配布処理が承認後に別の`.env`を読んで文面を無断再生成する方式にはしません。

### 最終表記のレビュー用生成

テンプレート確認後は`-DLICENSE_FINAL_REVIEW=ON`を追加すると、受領者向け本文から草案表示だけを除いた最終表記を生成できます。この場合は`-DSTAGE=<同梱対象のRelease配置>`、`-DCRT_BIN=<正規のRelease CRT>`、`-DAPPLICATION_BUILD=<対象アプリの構築先>`も指定し、MSVC開発者環境で実行します。

実配置の全ファイル・SHA256・コンポーネント対応を収集資料と照合し、対象アプリのソース識別子をソース一覧と照合します。CRTは署名・x64・原本一致・版を再検証して具体的な版を表示し、本体の版には`.env`の`BUILD_VERSION`を用います。例示URLを最終表記へ流用しません。対応ソースには`licenses/`と`.env.example`を含め、実際の`.env`は含めません。

このモードでも`manifest.review.json`と`review_only`を維持します。本文の草案表示の有無と配布承認は別であり、承認済みmanifestへの自動昇格、公開、同意取得は行いません。レビュー補助資料は受領者向け文面の外に分離します。

共通文書・対応表・ソース一覧は両言語ともmanifestへ登録し、コンポーネント別通知も英語正本と日本語訳を登録します。Markdownのリンク先を自動収集する前提にはしません。任意のQt SBOM入力は`evidence/`のJSONとして保存し、配布文面には展開しません。以前の監査経緯を含む通知文面も配布用本文へ引き継ぎません。

テンプレート生成後は連続する空白行を1行へまとめます（空白・タブだけの行も含む）。第三者原文のコピーにはこの整形を適用しません。情報ダイアログは文書ごとの拡張子と相対パスを保持し、`.md`はMarkdown、その他の原文はプレインテキストとして表示します。登録済み埋込み文書への相対リンクはクリックで遷移し、対応する一覧項目も切り替わります。文書から任意のローカルファイルを開いたり、画像の読込みでネットワークへ接続したりしません。HTTP(S)リンクは明示クリック時のみ既定ブラウザへ渡します。

提供済み依存アーカイブの直接利用経路を追加し、構築用Solid／WinFlexBisonも収集対象にしました。[アーカイブからの構築](build.md#再ビルド別環境)と固定Gitをネットワーク再取得する通常経路は区別します。最新23アーカイブから標準再構築・同一EXEへの改変DLL差替えに成功し、隔離Windowsで通常／改変各247件が失敗・スキップなしで通過しました。[検証記録](../../agent/source-rebuild-regression-followup.md)。その後の最終通知・ライセンス資料付き製品ZIPとソースZIPの生成、及び利用者による動作・表示・再構築性の確認をもって配布物監査は完了しました。[最終確認記録](../../agent/license-display-final-zips.md)。公開・適切な配布は配布者の責務です。

ライセンスmanifestの形式例（全DLLを一意にコンポーネントへ割り当てます）：

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

これはQt部分だけの例です。KDE各ライブラリ、QtKeychain、SASLと機構、GNU、CRT等も追加します。通知ファイルはmanifestからの相対パスに置く、空でないUTF-8テキストです。生成物の`originals/`には原文を未改変で保持します。情報ダイアログとZIPには同じ生成済み・承認済み資料を使います。ソース側の開発経緯`THIRD_PARTY_NOTICES.md`は、生成済み資料の代わりに製品へ同梱・埋込みしません。

## 2. 最終ZIPを生成する

リポジトリルートのMSVC x64開発者コマンドプロンプトで実行します。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DLEGAL_DIR=C:/release-licenses -DOUTPUT=dist/SmoothMail-windows-x64.zip -P cmake/PackageWindows.cmake
```

依存取得・Release構築、アプリの配布用Release構築、DLL／プラグイン／CRT・ライセンス資料の収集、個人パス・Debug混入等の検査、ZIP化を行います。ZIP内では`SmoothMail/licenses/`に配置し、未改変の原文はその`originals/`以下に置きます。`LEGAL_DIR`／`SMOOTHMAIL_LEGAL_DIR`という既存の引数名は互換性のため維持しますが、格納先を`legal/`にはしません。CRTの場所を一意に決定できない場合は `-DCRT_BIN=.../VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT` を追加します。既存ZIPは上書きしません。

`LEGAL_DIR`には監査済みの生成物を実際に配置してください。通常ディレクトリと、空ではない通常ファイルの`manifest.json`がない場合は、依存取得を始める前に停止します。テンプレートだけ、又は`manifest.review.json`の改名は代用になりません。manifestの内容と参照文書は引き続きpackage helperで検査します。過去の`build/`を削除した場合は、保存した対応ソースZIP等から生成済みの`licenses/`を復元します。

配布補助・依存構築の修正を利用しつつ、確認済みアプリのソースを維持する場合は、`-DAPPLICATION_SOURCE_DIR=<展開済み本体ソース>`を指定できます。省略時は従来どおり当リポジトリを構築します。アーカイブ展開ソースでは、その製品に対応する`-DSMOOTHMAIL_SOURCE_COMMIT=<commit>`を指定し、生成済み通知の対応ソースと一致させます。この指定は依存検査・配布用Release設定・個人情報検査を省略するものではありません。

WinFlexBisonの構築は、原本とは別のパッチ適用済み作業コピーを使い、EXE・Bison dataを構築先へ出力します。上流の原本内出力によって再実行時のtree pin検査に失敗しないようにするもので、原本検査の除外・緩和は行いません。

CRTはフォルダ全体を一括同梱せず、EXE・同梱DLL・指定プラグインの通常/遅延importと、そのCRTの推移依存から必要なDLLだけを選びます。VS 2022のRelease再配布用フォルダから取得し、Microsoft署名・x64・版の整合・コピー前後の同一性を検査します。署名確認はローカルの信頼情報を使うため、ネットワーク不要ですがオンライン失効確認を代替しません。未解決・未対応のCRT importや検査失敗時は停止します。実行時に独自に動的ロードするDLLや追加プラグインは、別途機能試験で確認してください。

CRT検証器の構築cacheはMSVCのパス長制限を避ける短い名前を使い、完全なソース識別ハッシュも別途保持して衝突時の再利用を拒否します。この修正以前の配布ソースを使う場合、展開先が深いと検証器の構築が失敗することがあります。その場合も検査を省略せず、ソースを短い新規パスへ展開して実行してください。

未解決・競合DLL、指定外のDLL、通知不足、未承認の埋込みパスで停止したら、入力を修正して再実行します。検査を無効化したりDLLを事後書換えしたりしません。供給元由来の情報は `cmake/runtime-path-policy.json` のハッシュ・範囲が一致するものに限り許容し、作業者情報はその範囲でも拒否します。

## 3. 成果物を検証する

1. ZIPを別の空フォルダへ展開し、`runtime-sha256.txt` の全ファイルをSHA256で照合する。
2. PDB、Debug CRT、DB、ログ、資格情報、登録JSON、ビルドキャッシュ・補助ツールがないことと、legalの対応を確認する。自動検査だけで任意の個人情報の不存在を保証しない。
3. 開発ツール・Qt SDKのないWindowsで、空白を含むパスへ展開し、開発用PATHなしでEXEを直接起動する。
4. テスト用アカウントで登録・再起動・更新・閲覧・送受信・下書き・添付・トレイ終了を確認する。実通信やメール作成は本人の許可を得る。
5. 同梱ライブラリの改変版への差替え、必要な再構築と通知の閲覧を確認する。情報ダイアログの版・コミットと、公開する対応ソースを一致させる。

合成テストは[テスト手順](test.md)を参照してください。Debug実行や合成ZIPの成功を、この最終検証の代わりにはできません。

## 4. 公開する

配布作業担当者は、生成済み通知の配布URLと一致する同じ配布ページへ、同じ版のバイナリZIP、対応ソース一式、各SHA256、通知・再構築／差替え手順を正当に掲載します。ソースを後日公開する前提や未履行の書面オファーで、先にバイナリだけを出しません。公開後は受領者としてバイナリ・ソース・通知を取得できることを確認します。これらは配布者の運用責務であり、完了した配布物監査の未完了項目としては扱いません。当記録の更新によって公開や取得確認を実施したものではありません。

## ライセンス上の注意

- SmoothMail本体のMITと、同梱依存の条件は別です。Qtはオープンソース条件を採用し、対象モジュールのLGPLと、内蔵第三者コード固有の条件を守ります。
- 原文・著作権・必須謝辞を削除しません。KDE等はファイル単位の条件やヘッダ・生成物・静的組込みも確認します。KIOのfavicon除外だけで残り全体の許諾を認定しません。
- 変更済み依存は `patches/` と [第三者通知](../../THIRD_PARTY_NOTICES.md)に従い、原本・差分・変更内容／日付・機能制限・再構築に必要な入力を対応ソースへ含めます。
- LGPL等の改変・差替え・必要なデバッグの権利を追加条件で妨げません。提供形態に応じて必要なInstallation Informationも確認します。
- **MSVC CRTは必要なRelease DLLを改変せずZIPへ同梱します。MSVC 2022の正当な利用と、適用されるMicrosoftの使用許諾条件・REDIST対象の確認/遵守は、配布作業担当者の責務です。** 有料版が一律に必要という意味ではありません。自動検査は担当者の利用資格を認定せず、追加の利用資格承認待ちとして作業を止める仕組みにもしません。
- **配布作業担当者は、ダウンロードページの利用者の目につく明瞭な位置へ、当ソフトウェアをダウンロードした時点で同梱MSVC CRTの利用許諾条件に同意したとみなす旨を表示します。** 当表示の実施と適用契約への適合性の確認は配布者の責務です。アプリ内のライセンス表示や監査完了の記録だけで、公開ページの表示・同意取得まで実施済みとは扱いません。
- Microsoft CRTをSmoothMailのMITやQtのLGPLへ再ライセンスしません。Microsoftの通知・配布条件を保持し、適用される同意条件も確認します。CRTの条件を他のOSSの改変・差替え権まで制限するように一括適用しません。[公式REDIST一覧](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)、[VS 2022使用許諾条項](https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/)を参照してください。
- Debug CRT・再配布対象外ファイル・不要なビルドツールを同梱しません。同梱CRTのセキュリティ更新はアプリ配布側が行い、中央配備されたCRTの更新だけには依存しません。

自動検査は法的条件や配布権限を認定しません。今回の配布物監査完了は技術検査と利用者の最終確認に基づくものであり、配布者の継続的な責務を免除しません。対象成果物を変更する場合は必要な再照合・レビューを行います。
