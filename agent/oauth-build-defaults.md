# OAuth既定値とシステム設定

- CMake構成時にGit管理外の`.env`をデータとして読み込み、既定クライアントID／Google Desktopシークレットをbuild下の`generated/oauthdefaults.h`へ生成する。値はログやCMakeCacheへ出力しない。
- `.env`がない場合は`.env.example`の空の登録値を使用する。実ファイルは変更・コピー・コミットしていない。起動プロセスのOAuth用環境変数は本体から参照しない。
- Gmail／Outlookそれぞれに既定使用フラグと独自IDをQSettingsで保存する。新規設定は既定使用ON。移行時は既存の非空Google独自IDがあれば独自設定を維持する。
- 独自Googleシークレットは従来のOS資格情報ストアキーへ保存し、既定使用中も独自設定の入力値を保持する。保存失敗のロールバック対象に追加キーも含める。
- 新規認可は選択中の設定を使用する。保存済みgrantは元のclient IDに結び付くので、refresh／再認可では元のIDに対応する既定または独自シークレットを選ぶ。既存アカウントの登録ID変更は編集画面で認可し直して保存する。
- Google Desktopのシークレットはネイティブアプリへ埋め込む登録情報であり、機密性の根拠とはしない。PKCE／state／TLS検証を維持する。
- CMakeとPythonのライセンス生成は同じ`.env`のOAuthキーを受理する。文面の置換対象にはせず、Pythonから返すメタデータからも除外する。

## 検証（2026-11-17）

- `build/spec-changes-check.cmd`のWindows/MSVC Releaseビルド成功。
- `build/spec-changes-test.cmd`のCTest全26ターゲット成功（実OS資格情報テストは既存のopt-inに従いスキップ）。
- `build/oauth-spec-regression.cmd`のFakeNetworkによるrefresh・登録エラー・環境変数非参照検証は5 passed（init/cleanup含む）、0 failed、0 skipped。結果は`build/oauth-spec-regression-results.txt`。
- CMakeのOAuth生成テストは空の例示値／引用符付き既定値／環境変数の無視／重複・未知キー拒否／診断への値漏出防止を検証する。システム設定UIでは独自入力の有効化・保存通知・失敗後の保持・既定に戻した際の無効化を確認。
- 実Google／Microsoftへのブラウザ認可・送受信は今回実行していない。
