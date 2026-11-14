# GNU runtimeの隔離・再配置試験

Windows/MSVC、既存Qt Core、構築済み`native/gnu-runtime-v1`を使用するC++試験です。
アプリ側は`SMOOTHMAIL_TEST_NATIVE=ON`で`native_runtime_relocated`をCTestへ登録します。
単独でconfigure/buildする場合は、`Qt6_ROOT`と必要なら`NATIVE_BIN`を指定し、QtのbinをPATHへ追加してCTestを実行します。

- GNU DLLの実ロード元を確認し、別DLLへのフォールバックを拒否します。
- bind前の既定カタログパスが`/share/gettext-libintl/locale`であることを確認し、UNC/MSYSパス化を検出します。
- Shift-JIS→UTF-8、不完全な入力の拒否を確認します。
- 自前生成のGNU `.mo`にある日本語訳・単数/複数形を公開APIで読みます。
- DLLとカタログを空白・日本語を含む別フォルダへ移して、別プロセスで再検証します。
- gettextはKI18nと同様に、カタログの移動先を明示してbindします。既定ディレクトリの自動再配置を保証する試験ではありません。
- LANGUAGEは子プロセス起動前に固定します（Debug QtとRelease GNUの異なるCRTの環境配列にも対応）。

作業先は`build/native-runtime-tests/`以下の新規領域です。実設定・メール・資格情報は使いません。
localeと環境変数の変更は試験プロセス内だけで、OS設定を変更しません。
この試験だけでGNUの全APIや製品配布全体を検証したとは扱いません。
