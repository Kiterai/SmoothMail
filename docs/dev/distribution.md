# Windows版の配布

Windows x64向けポータブルZIPと対応ソース一式を生成します。**生成と監査・公開は別の工程です。** 生成前の手動ライセンス承認やmanifest編集は不要ですが、生成成功だけでは公開できません。

## 最終配布物の生成

### .envの設定

[ビルド要件](build.md)を満たすクリーンなcloneで、Git管理外のUTF-8の`.env`を作成します。`.env.example`を参考に次を記述してください。

| 変数 | 内容 |
|---|---|
| `SOFTWARE_NAME` | ソフトウェア表示名（必須） |
| `DISTRIBUTION_URL` | バイナリと対応ソースを提供する実際のHTTPSページ（必須、例示URL不可） |
| `BUILD_VERSION` | 配布版のバージョン文字列（必須） |
| `SMOOTHMAIL_QT_ROOT` | 既存MSVC Qt SDKのパス（必須） |
| `SMOOTHMAIL_DEPENDENCY_ROOT` | 依存保存先。空値なら`build/deps` |
| `SMOOTHMAIL_CRT_BIN` | 正規の`VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT`。空値ならMSVC開発者環境から探索 |
| `SMOOTHMAIL_DOWNLOAD_CURL` | 制限環境で使うOpenSSL対応curlの絶対パス。空値ならCMakeのHTTPS取得 |
| `SMOOTHMAIL_SOURCE_ARCHIVES` | 提供済み依存原本アーカイブの置場。通常は空値で自動取得 |
| `SMOOTHMAIL_ASSET_CACHE` | 外部ビルドツール取得物の再利用先。通常は空値 |
| `SMOOTHMAIL_SOURCE_COMMIT` | Gitのない展開ソースを低水準入口で再構築する際の識別子。通常は空値。配布生成では検証したHEADを使用 |
| `SMOOTHMAIL_GOOGLE_CLIENT_ID` | Google Desktop用OAuthクライアントID。空値可 |
| `SMOOTHMAIL_GOOGLE_CLIENT_SECRET` | Google Desktop用登録シークレット。空値可 |
| `SMOOTHMAIL_MICROSOFT_CLIENT_ID` | Microsoftネイティブアプリ用OAuthクライアントID。空値可 |

`KEY=value`形式で値全体の引用符は使用できます。パス区切りには`/`を使います。シェル実行・変数展開は行わず、未知キー・重複キー・不正値は拒否します。OAuth値は本体の既定設定へ埋め込みますが、通知・メタデータには出力しません。`.env`自体は同梱しません。Google Desktop用シークレットは成果物から秘匿できる情報とは扱いません。OAuthが空値の場合は利用者による独自登録が必要です。変更後は再構成・再ビルドします。

### 生成コマンド

リポジトリルートの **x64 Native Tools Command Prompt for VS 2022** で実行します。

```bat
python tools/build.py dist
```

この26文字の単一コマンドは[Releaseビルド](build.md#リリースビルド)と同じpreset・処理を使います。依存取得・Release構築・DLL／プラグイン／CRT配置・原文／対応ソース収集・英日通知生成・通知埋込み再リンク・自動検査・ZIP化・実行環境の展開照合までを行います。手動でのツール追加導入、資料生成、承認manifest作成やZIP展開は不要です。

出力：

- `build/rel/artifacts/SmoothMail-windows-x64.zip`：本体・ランタイム・通知・利用案内。
- `build/rel/artifacts/sources/`：本体と依存の対応ソース原本23アーカイブ、識別子・ハッシュの一覧、再構築案内。本体ソースにパッチ・構築入力・通知テンプレート・再構築／差替え手順を含みます。
- `build/rel/artifacts/SHA256SUMS.txt`：ZIPと対応ソース資料のSHA256。
- `build/rel/artifacts/AUDIT-REQUIRED.txt`：生成と公開承認が別である旨の注意。
- `build/rel/run/`：ZIPと全ファイルが同一の、追加作業なく起動できる実行環境。
- `build/rel/jobs/<識別子>/`：生成資料と検査証拠。`final-evidence/`は通知埋込み後のランタイムに対応します。作業用証拠はそのまま公開しません。

ZIPの`SmoothMail/`直下には`licenses/`、`runtime/`、`README.md`、`storage.md`、`launch.cmd`、`runtime-sha256.txt`を収録します。READMEとstorageは`docs/usage.md`・`docs/storage.md`と同じ内容です。通知はダイアログにも同一原文・文面を埋め込みます。テンプレートは`licenses/common/`と`licenses/editions/windows-x64/`で管理し、英語を正本、日本語を訳文とします。第三者原文は未改変で保持します。

自動処理は原本ハッシュ、構成の不足・重複、対応ソース、CRTの署名・x64・版・原本一致、Debug混入、通知対応、既知の作業者パス等を検査します。構成・ハッシュ・パスの不一致では停止し、検査無効化やDLLの事後書換えは行いません。供給元パスの例外も`cmake/runtime-path-policy.json`の限定されたハッシュ・範囲だけです。再実行では新しいジョブへ生成し、前回成功出力を退避して保持します。

`tools/generate_licenses.py`の既定動作は引き続きレビュー草案のみです。統合入口は`--final-review --distribution`相当の完全性検査を伴う生成を行い、配布用`manifest.json`を新規作成します。これは法的承認ではありません。草案の改名や検査回避による承認は行いません。従来の`tools/package_windows.py --legal-dir ...`は既存資料向けの低水準入口であり、新規cloneからの標準手順ではありません。

## ライセンス注意事項

### 生成後・公開前に配布担当者が行う監査

1. 新しい展開先でハッシュ、全DLL・プラグインのコンポーネント割当、原文・著作権・謝辞・変更通知、英日文面、名称・版・URLを照合する。PDB、Debug CRT、DB、ログ、資格情報、登録JSON、キャッシュ、作業者の個人情報・PC内情報の非混入を確認する。
2. Qt／KDEと内蔵第三者コードのライセンス条件を確認する。本体MITと配布物全体の条件は区別する。KIOCoreのGPL部分除外、KConfigCore限定、テスト等の非同梱が対象バイナリと一致するか確認する。
3. 本体・Qt・依存すべての対応ソース、変更パッチ、構築入力、通知、アーカイブの版・ハッシュと実バイナリの対応を確認する。提供ソースからの再構築・ABI適合・改変DLL差替えを試験し、LGPL等の改変・差替え・必要なデバッグの権利と、該当するInstallation Informationの要否・充足を確認する。
4. MSVC 2022の正当な利用、適用契約・REDIST対象・再配布権限を確認する。必要なRelease CRTのみを無改変で同梱し、原本一致・版・署名・セキュリティ更新を確認する。自動署名検査はローカル信頼情報を使い、オンライン失効確認を代替しない。
5. Qt SDK・開発ツールのないWindowsで、空白を含むパス、開発用PATHなし、異なる作業ディレクトリから`launch.cmd`と`runtime/smoothmail.exe`を起動する。許可されたテストアカウントで登録・再起動・更新・閲覧・送受信・下書き・添付・トレイ終了を確認する。オフライン通知表示とZIP／埋込み通知の一致も確認する。

[合成テスト](test.md)や自動検査だけでは、実サービス・実配布物の動作、法的条件、翻訳の完全性、再配布権限や個人情報の完全非混入を保証しません。不備があれば入力・ソースを修正して再生成・再監査します。生成後のZIPだけを手修正しません。

### 公開時以外の注意

- 原本・パッチ・対応ソース・通知・監査記録・公開した成果物を版ごとに保管し、ライセンス上必要な期間の提供・問い合わせに対応する。
- 版・URL・依存・Qt SDK・CRT・文面の変更時は再生成・再監査する。CRT等のセキュリティ更新も配布担当者が管理する。
- LGPL等の改変・差替え権を契約・利用条件で妨げない。Microsoft CRTの条件をOSSへ一括適用せず、CRTをMIT／LGPLへ再ライセンスしない。
- [公式REDIST一覧](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)、[VS 2022使用許諾条項](https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/)と採用したQtのオープンソース条件を確認する。

## 配布手順

監査・動作確認を完了した同じ版について、次を`DISTRIBUTION_URL`と一致する**同じページ**へ掲載します。

- バイナリZIPとそのSHA256。
- `sources/`の全対応ソースアーカイブと識別子・SHA256一覧。
- ライセンス通知一式（ZIP内`licenses/`）と、本体ソース内`docs/dev/build.md`・`docs/dev/distribution.md`の再構築／差替え案内。

**ダウンロードした時点で同梱MSVC CRTの利用許諾条件に同意したとみなす旨を、ダウンロードページの利用者の目につく位置へ表示してください。** 表示の実施・適用契約への適合性は配布担当者が確認します。アプリ内表示で代替しません。

公開後、バイナリ・全ソース・ハッシュ・通知を実際に取得できることを確認します。未履行の書面オファーや後日公開を前提にバイナリだけを先行公開しないでください。自動生成は公開操作・法的監査・同意取得を実施しません。
