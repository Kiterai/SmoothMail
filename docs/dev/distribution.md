# Windows版の配布

Windows x64向けのポータブルZIPを配布します。展開した`SmoothMail/launch.cmd`から起動します。本体・DLL・プラグインは`runtime/`へまとめます。以下はリポジトリルートのMSVC x64開発者コマンドプロンプトで実行してください。

## 1. 入力を準備する

[ビルド要件](build.md)に加え、次を用意します。

- 追跡ファイルに変更がない、配布対象のGitコミット。
- Qt SDKのRelease DLL・プラグインと、MSVC 2022のx64 Release再配布用CRT。
- 対象バイナリと対応するソース一式、レビュー済みの生成ライセンス資料と`manifest.json`。

### 原文・対応ソースの収集

依存を構築し、[Release配置](run.md#リリースビルド実行)を作成した後に実行します。各出力先は新しいディレクトリにしてください。

```bat
cmake -P cmake/FetchQtLegalSources.cmake
cmake -DSTAGE=build/run-release -DOUTPUT=build/legal-review -P cmake/PrepareWindowsLegalReview.cmake
cmake -DOUTPUT=build/source-review -P cmake/PrepareWindowsSourceReview.cmake
cmake -DREVIEW=build/source-review -P tests/cmake/verify_source_review.cmake
```

依存rootを変更した場合は収集にも`-DDEPENDENCY_ROOT=...`を指定します。ソース収集はHEADを使うため、配布対象の変更は先にコミットしてください。[提供アーカイブからの再構築](build.md#再ビルド別環境)も確認します。

収集結果はレビュー用です。原文・著作権・変更差分・再構築／DLL差替え・バイナリとの対応を確認してください。`review_only`資料は改名しても配布入力として拒否されます。

### 文面の生成と確認

テンプレートは[`licenses/common/`](../../licenses/common/)と[`licenses/editions/windows-x64/`](../../licenses/editions/windows-x64/)で管理します。英語`filename.md`が正本、日本語`filename.ja.md`は常体の訳文です。第三者原文は未改変のまま`originals/`へ保持します。

`.env.example`を参考に、Git管理外のUTF-8の`.env`へ次を設定します。

| 変数 | 内容 |
|---|---|
| `SOFTWARE_NAME` | 表示するソフトウェア名称 |
| `DISTRIBUTION_URL` | バイナリと対応ソースを提供するHTTPSページ |
| `BUILD_VERSION` | 対象ビルドの版情報 |

`KEY=value`形式で、値全体の引用符は使用できます。シェル実行・変数展開は行いません。空値・未知キー・不正URL等は拒否します。アプリ資格情報用の設定と兼用せず、必要なら`-DENV_FILE=<専用ファイル>`を指定してください。実`.env`は同梱しません。`.env.example`のURLはレビュー例示用です。

```bat
cmake -DEDITION=windows-x64 -DEVIDENCE=build/legal-review -DSOURCE_REVIEW=build/source-review -DOUTPUT=build/windows-licenses-review -P cmake/PrepareWindowsLegalDraft.cmake
```

草案表示を除いた最終表記を確認する場合は、次も指定します。

- `-DLICENSE_FINAL_REVIEW=ON`
- `-DSTAGE=<対象Release配置>`
- `-DAPPLICATION_BUILD=<対象アプリ構築先>`
- `-DCRT_BIN=<正規のx64 Release CRT>`

全配置ファイルのハッシュ・コンポーネント割当・ソース識別子とCRTの由来／版を検査します。このモードでも未承認の`manifest.review.json`を維持し、配布用入力へ自動昇格しません。

生成物の`README.ja.md`から文面・版・URL・対応ソースをレビューします。生成文書の連続空白行は1行に整理し、第三者原文には整形を適用しません。本文・原文・英日文書をmanifestへ登録し、リンク先の自動収集に依存しない構成にします。必要なら`-DQT_SBOM_REVIEW=<JSON>`で監査補助資料を添付できます。

レビュー後は別ディレクトリに配布用`manifest.json`を用意します。形式は`schema: 1`と`components`を持ち、`review_only`を含まないものです。各コンポーネントの名称・版・対象ファイル・原文・ソース通知を登録し、全同梱DLLを一意に割り当てます。参照文書は空でないUTF-8テキストとします。

名称・URL・版・構成・文面を変更した場合は再生成・再レビューしてください。配布処理には確認済みの資料を渡し、ZIPとダイアログへ同じ文書を収録します。開発経緯の`THIRD_PARTY_NOTICES.md`は生成済み通知の代用にしません。

## 2. 最終ZIPを生成する

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DLEGAL_DIR=C:/release-licenses -DOUTPUT=dist/SmoothMail-windows-x64.zip -P cmake/PackageWindows.cmake
```

依存取得・Release構築、アプリ構築、DLL／プラグイン／CRT・通知の収集、個人情報・Debug混入・ライセンス対応の検査、ZIP化を行います。通知は`SmoothMail/licenses/`、本体は`SmoothMail/runtime/Smoothmail.exe`、全ファイルのハッシュは`SmoothMail/runtime-sha256.txt`へ配置します。`README.md`には対象本体ソースの`docs/usage.md`をそのまま収録します。既存ZIPは上書きしません。

`LEGAL_DIR`に通常ディレクトリと空ではない通常ファイルの`manifest.json`がない場合は、依存取得前に停止します。保存先を削除した場合は、保管したソースZIP等から生成済み`licenses/`を復元してください。

主な追加指定：

| 引数 | 用途 |
|---|---|
| `CRT_BIN` | 自動探索できない場合の`VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT` |
| `DEPENDENCY_ROOT` | 依存の構築・保存先 |
| `APPLICATION_SOURCE_DIR` | 当リポジトリとは別の、確認済み本体ソース |
| `SMOOTHMAIL_SOURCE_ARCHIVES` | 提供済み依存アーカイブのディレクトリ |
| `SMOOTHMAIL_SOURCE_COMMIT` | アーカイブ展開した本体のソース識別子 |
| `SMOOTHMAIL_DOWNLOAD_CURL` | 制限環境で使う[OpenSSL対応curl](build.md#windowsの制限付きサンドボックスでのhttps取得)の絶対パス |

本体ソース・コミット・生成済み通知の対応を一致させてください。短い構築パスを推奨します。

CRTは通常／遅延importと推移依存から必要なRelease DLLだけを選択し、署名・x64・版・原本一致を検査します。署名確認はローカルの信頼情報を使い、オンライン失効確認の代用にはなりません。動的ロードする追加DLLやプラグインは機能試験でも確認します。

入力やハッシュ・パスの不一致で停止した場合は原因を確認し、検査を無効化したりDLLを事後書換えしたりしないでください。供給元由来の情報は`cmake/runtime-path-policy.json`の全体ハッシュと許容範囲に一致するものだけを認め、作業者情報は範囲内でも拒否します。

## 3. 成果物を検証する

1. 新しいフォルダへZIPを展開し、`runtime-sha256.txt`の全ファイルを照合する。
2. PDB、Debug CRT、DB、ログ、資格情報、登録JSON、ビルドキャッシュ・補助ツールの非混入と通知の対応を確認する。
3. 開発ツール・Qt SDKのないWindowsで、空白を含むパスと開発用PATHなしで`launch.cmd`を別の作業ディレクトリから起動する。`runtime/Smoothmail.exe`の直接起動も確認する。
4. 許可されたテストアカウントで登録・再起動・更新・閲覧・送受信・下書き・添付・トレイ終了を確認する。
5. 対応ソースからの再構築・改変DLL差替え、オフライン通知表示、版・コミット・同梱通知と埋込み文書の一致を確認する。

[合成テスト](test.md)の成功だけで実サービス・実配布物の確認を代替しません。

## 4. 公開・ライセンス上の注意

公開・正当な配布と、次の事項の実施は配布作業担当者の責務です。

- 生成済み通知のURLと一致する同じページに、同じ版のバイナリ・対応ソース一式・各SHA256・通知・再構築／差替え手順を掲載し、公開後に取得を確認する。未履行の書面オファーや後日ソース公開を前提にバイナリだけを先行公開しない。
- Qtはオープンソース条件を採用する。本体MIT、Qt／KDE等と内蔵第三者コードの条件を区別し、原文・著作権・必要な謝辞を保持する。
- 変更済み依存の原本・パッチ・変更通知・構築入力を対応ソースへ含める。LGPL等の改変・差替え・必要なデバッグの権利を妨げず、必要なInstallation Informationも確認する。
- 必要なMSVC Release CRTだけを未改変で同梱する。MSVC 2022の正当な利用、適用契約・REDIST対象の確認／遵守、CRTのセキュリティ更新を担う。
- **ダウンロードした時点で同梱MSVC CRTの利用許諾条件に同意したとみなす旨を、ダウンロードページの利用者の目につく位置へ表示する。** 表示の実施と適用契約への適合性も配布者が確認する。
- Microsoft CRTをMITやLGPLへ再ライセンスせず、CRTの条件を他のOSSの改変・差替え権へ一括適用しない。[公式REDIST一覧](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)、[VS 2022使用許諾条項](https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/)を確認する。

自動検査は法的条件・配布権限や個人情報の完全非混入を保証しません。アプリ内表示や監査完了だけで、公開ページの表示・同意取得まで実施済みとは扱わないでください。
