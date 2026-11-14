# Windows版の配布

配布形式はWindows x64向けポータブルZIPです。展開した `SmoothMail/smoothmail.exe` を直接起動します。

**現時点ではライセンス最終監査・legal付き実ZIP・クリーンWindowsでの最終確認は未完了です。** 以下を満たすまで公開しないでください。残作業は[未完了仕様](../../spec/inprogress/distribution.md)で管理します。

## 1. 入力を準備する

[ビルド要件](build.md)に加え、次を準備します。

- 配布するソースを確定した、追跡ファイルに変更のないGitコミット。
- Qt SDKのRelease DLL・プラグインと、MSVC 2022のx64 Release再配布用CRT。
- 全同梱コンポーネントの原文・著作権・変更通知・対応ソース案内をまとめたlegalディレクトリと、レビュー済みの `manifest.json`。

依存の構築後、以下で通知と対応ソースの**レビュー用資料**を収集できます。`STAGE` には [Release配置手順](run.md#リリースビルド実行)で作成した候補を指定し、各 `OUTPUT` は新しい保存先にします。

```bat
cmake -P cmake/FetchQtLegalSources.cmake
cmake -DSTAGE=build/run-release -DOUTPUT=build/legal-review -P cmake/PrepareWindowsLegalReview.cmake
cmake -DOUTPUT=build/source-review -P cmake/PrepareWindowsSourceReview.cmake
cmake -DREVIEW=build/source-review -P tests/cmake/verify_source_review.cmake
```

別の依存rootを使った場合は収集コマンドにも `-DDEPENDENCY_ROOT=...` を指定します。ソース収集はHEADの追跡ソースを使うため、配布対象の変更は事前にコミットしてください。

`manifest.review.json`・`source-manifest.review.json` は完成品の証明ではありません。原文と実際の組込みコード、全依存と変更差分、再構築・DLL差替え・最終バイナリとの対応を確認します。`review_only` 資料は改名しても配布入力として拒否されます。

提供済みの依存アーカイブを直接使う経路には、Git管理情報なしでは既存の依存ビルド入口が停止し、構築用Solid／WinFlexBisonアーカイブも別途必要になる問題があります。固定Gitソースをネットワーク再取得する通常経路とは区別してください。補助入力付きの直接再構築・DLL差替えは検証しましたが、提供物だけからの標準再構築成功とは扱いません。[残件と検証結果](../../spec/inprogress/distribution.md#dist-02-配布物の監査)を解消してから確定してください。

legalの形式例（全DLLを一意にコンポーネントへ割り当てます）：

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

これはQt部分だけの例です。KDE各ライブラリ、QtKeychain、SASLと機構、GNU、CRT等も追加します。通知ファイルはmanifestからの相対パスに置く、空でないUTF-8テキストです。情報ダイアログとZIPには同じ承認済みlegalを使います。

## 2. 最終ZIPを生成する

リポジトリルートのMSVC x64開発者コマンドプロンプトで実行します。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DLEGAL_DIR=C:/release-legal -DOUTPUT=dist/SmoothMail-windows-x64.zip -P cmake/PackageWindows.cmake
```

依存取得・Release構築、アプリの配布用Release構築、DLL／プラグイン／CRT・legalの収集、個人パス・Debug混入等の検査、ZIP化を行います。CRTの場所を一意に決定できない場合は `-DCRT_BIN=.../VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT` を追加します。既存ZIPは上書きしません。

CRTはフォルダ全体を一括同梱せず、EXE・同梱DLL・指定プラグインの通常/遅延importと、そのCRTの推移依存から必要なDLLだけを選びます。VS 2022のRelease再配布用フォルダから取得し、Microsoft署名・x64・版の整合・コピー前後の同一性を検査します。署名確認はローカルの信頼情報を使うため、ネットワーク不要ですがオンライン失効確認を代替しません。未解決・未対応のCRT importや検査失敗時は停止します。実行時に独自に動的ロードするDLLや追加プラグインは、別途機能試験で確認してください。

未解決・競合DLL、指定外のDLL、通知不足、未承認の埋込みパスで停止したら、入力を修正して再実行します。検査を無効化したりDLLを事後書換えしたりしません。供給元由来の情報は `cmake/runtime-path-policy.json` のハッシュ・範囲が一致するものに限り許容し、作業者情報はその範囲でも拒否します。

## 3. 成果物を検証する

1. ZIPを別の空フォルダへ展開し、`runtime-sha256.txt` の全ファイルをSHA256で照合する。
2. PDB、Debug CRT、DB、ログ、資格情報、登録JSON、ビルドキャッシュ・補助ツールがないことと、legalの対応を確認する。自動検査だけで任意の個人情報の不存在を保証しない。
3. 開発ツール・Qt SDKのないWindowsで、空白を含むパスへ展開し、開発用PATHなしでEXEを直接起動する。
4. テスト用アカウントで登録・再起動・更新・閲覧・送受信・下書き・添付・トレイ終了を確認する。実通信やメール作成は本人の許可を得る。
5. 同梱ライブラリの改変版への差替え、必要な再構築と通知の閲覧を確認する。情報ダイアログの版・コミットと、公開する対応ソースを一致させる。

合成テストは[テスト手順](test.md)を参照してください。Debug実行や合成ZIPの成功を、この最終検証の代わりにはできません。

## 4. 公開する

同じ配布ページへ、同じ版のバイナリZIP、対応ソース一式、各SHA256、通知・再構築／差替え手順を掲載します。ソースを後日公開する前提や未履行の書面オファーで、先にバイナリだけを出しません。公開先は未設定です。公開後は受領者としてバイナリ・ソース・通知を取得できることを確認します。

## ライセンス上の注意

- SmoothMail本体のMITと、同梱依存の条件は別です。Qtはオープンソース条件を採用し、対象モジュールのLGPLと、内蔵第三者コード固有の条件を守ります。
- 原文・著作権・必須謝辞を削除しません。KDE等はファイル単位の条件やヘッダ・生成物・静的組込みも確認します。KIOのfavicon除外だけで残り全体の許諾を認定しません。
- 変更済み依存は `patches/` と [第三者通知](../../THIRD_PARTY_NOTICES.md)に従い、原本・差分・変更内容／日付・機能制限・再構築に必要な入力を対応ソースへ含めます。
- LGPL等の改変・差替え・必要なデバッグの権利を追加条件で妨げません。提供形態に応じて必要なInstallation Informationも確認します。
- **MSVC CRTは必要なRelease DLLを改変せずZIPへ同梱します。MSVC 2022の正当な利用と、適用されるMicrosoftの使用許諾条件・REDIST対象の確認/遵守は、配布作業担当者の責務です。** 有料版が一律に必要という意味ではありません。自動検査は担当者の利用資格を認定せず、追加の利用資格承認待ちとして作業を止める仕組みにもしません。
- Microsoft CRTをSmoothMailのMITやQtのLGPLへ再ライセンスしません。Microsoftの通知・配布条件を保持し、適用される同意条件も確認します。CRTの条件を他のOSSの改変・差替え権まで制限するように一括適用しません。[公式REDIST一覧](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)、[VS 2022使用許諾条項](https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/)を参照してください。
- Debug CRT・再配布対象外ファイル・不要なビルドツールを同梱しません。同梱CRTのセキュリティ更新はアプリ配布側が行い、中央配備されたCRTの更新だけには依存しません。

自動検査は法的条件や配布権限を認定しません。監査の未確認事項は[未完了仕様](../../spec/inprogress/distribution.md)に記録します。
