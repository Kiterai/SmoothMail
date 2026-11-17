# テスト

## Pythonバッチ処理

```text
python -m unittest discover -s tests/python -v
```

配布入口の事前検査、ランタイム依存解決、原本・テンプレート・生成manifest、コマンド長と統合処理、依存図変換、ソースディレクトリの循環依存・層違反を検査します。試験自体はビルドから自動実行しません。MSVC CRTの署名検証と最終ZIPの検証には、以下のネイティブ試験も実行してください。

クラス依存解析を含めた試験は、文書生成環境の導入後に`python tools/generate_architecture.py --test`で実行します。通常のPythonに解析用依存がない場合、その試験だけはスキップします。

[Debugビルド](build.md)後、リポジトリルートのMSVC x64開発者コマンドプロンプトで実行します。以下は標準の依存rootを使う例です。別rootの場合はパスを読み替えてください。

## 通常の自動テスト

```bat
set PATH=C:\Qt\6.12.0\msvc2022_64\bin;%CD%\build\deps\install\Debug\bin;%CD%\build\deps\native\gnu-runtime-v1\x64-windows\debug\bin;%CD%\build\deps\native\x64-windows\tools\gettext\bin;%PATH%
ctest --test-dir build/dbg/app --output-on-failure
```

GUI単体テストはoffscreenで動作します。OS資格情報への書込みや意図的クラッシュを伴うケースは、通常環境ではスキップします。成功件数だけでなくスキップ内容も確認してください。個別実行は `ctest --test-dir build/dbg/app -R "^mainwindow$" --output-on-failure` のように指定します。

## 依存・通信の追加テスト

同じビルド先へオプションを追加して再構築します。

```bat
cmake -S . -B build/dbg/app -DSMOOTHMAIL_TEST_SASL=ON -DSMOOTHMAIL_TEST_KDE_BASE=ON -DSMOOTHMAIL_TEST_KIO=ON -DSMOOTHMAIL_TEST_MAIL=ON -DSMOOTHMAIL_TEST_NATIVE=ON
cmake --build build/dbg/app --parallel 4
ctest --test-dir build/dbg/app --output-on-failure
```

SASL機構、KDE API、GNU DLL再配置、合成IMAP／SMTP・TLS・OAuthを検査します。通信系はループバックTCPが必要です。実サービスの認証や実メールは使いません。制限されたシェルで接続が拒否される場合は、テストを成功扱いせず、ループバックの使える隔離環境で実行してください。

## Windows SandboxでのGUI・資格情報テスト

1. 上記に `-DSMOOTHMAIL_BUILD_GUI_SMOKE=ON` を追加して構築します。`tst_guismoke` はCTestには登録されません。
2. [Debugの実行環境](run.md#デバッグ実行)を専用フォルダへコピーし、テストEXE・対応するQt Test DLL・SDKの `plugins/platforms/qoffscreend.dll`（offscreenテスト用）を追加した**専用テストセット**を用意します。通常起動フォルダを上書きしません。
3. 外部通信を無効にした使い捨てWindows Sandboxへコピーします。実メール、登録JSON、ホストのDB・資格情報は共有しません。ログ回収用の専用フォルダだけを書込み可能にします。
4. ゲスト内で次を実行します（配置例は `C:\MailTest`、回収先は `C:\TestOutput`）。

```bat
cd /d C:\MailTest
set PATH=C:\MailTest;C:\Windows\System32;C:\Windows
set QT_PLUGIN_PATH=C:\MailTest
set SMOOTHMAIL_TEST_SASL_PATH=C:\MailTest\sasl2
set SMOOTHMAIL_GUI_OUTPUT=C:\TestOutput
set SMOOTHMAIL_TEST_KEYCHAIN=1
set SMOOTHMAIL_GUI_SANDBOX_TEST=1
set QT_QPA_PLATFORM=windows
tst_guismoke.exe -o C:\TestOutput\guismoke.txt,txt
tst_credentialstore.exe -o C:\TestOutput\credentialstore.txt,txt
tst_oauthclient.exe -o C:\TestOutput\oauthclient.txt,txt
tst_connectiontester.exe -o C:\TestOutput\connectiontester.txt,txt
tst_mailtransport.exe -o C:\TestOutput\mailtransport.txt,txt
tst_diagnostics.exe -o C:\TestOutput\diagnostics.txt,txt
tst_mimeregression.exe -o C:\TestOutput\mimeregression.txt,txt
```

`C:\TestOutput` は先に作成してください。各EXEの終了コード・失敗・スキップと画面記録を確認し、回収後はSandboxを終了します。上記のopt-in変数はホストで設定しないでください。

通常アプリでも、空状態の起動、アカウント入力・取消、一覧と本文、フィルタ・ページ移動・ソート、作成・返信、添付保存・履歴、トレイへの格納・復帰・終了を確認します。合成データによる確認と実サービスの確認は区別します。

## ビルド・配布処理のガード

ネットワーク不要のビルドガード：

```bat
cmake -P tests/cmake/run_build_guards.cmake
```

配布補助ツールのテスト（[ビルド](build.md)で内部Ninjaを準備済み）：

```bat
cmake -S tools/windows-package -B build/package-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DCMAKE_MAKE_PROGRAM=%CD%/build/deps/build/tools/ninja-79feac0f3e3bc9da9effc586cd5fea41e7550051/ninja.exe
cmake --build build/package-tests --parallel 4
ctest --test-dir build/package-tests --output-on-failure
```

配布補助CTestには原本保持・不正入力・ZIP・ライセンス・Qt attributionのガードが含まれます。個別実行は次の形式です。

```bat
cmake -P tests/cmake/run_package_entry_guards.cmake
```

| スクリプト（`tests/cmake/`） | 対象 |
|---|---|
| `run_crt_guards.cmake` | CRT import選択 |
| `run_crt_binary_guards.cmake` | CRT署名・版・原本一致、不正PE／改変コピー拒否 |
| `run_crt_long_path_guards.cmake` | 長い展開先での検証器構築・cache衝突拒否 |
| `run_source_archive_guards.cmake` | アーカイブ・パッチ適用・原本非改変、Qt SDK出力先保護 |
| `run_sasl_source_guards.cmake` | SASLの絶対探索既定値除去、原本保持、反復利用、設定改変時の停止 |
| `run_license_template_guards.cmake` | `.env`入力・変数置換・空白行整理 |
| `run_license_final_review_guards.cmake` | 配置ファイルの不足・改変・誤分類・不正パス |

CRT実バイナリの試験はMSVC x64開発者環境で行います。原本は変更せず、合成コピーは`build/`以下へ作成します。

Debug配置だけを実Qt／Debug CRTで検査する場合は、`cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -P tests/cmake/run_debug_staging.cmake`を実行します。小さなQtアプリとローダー用の合成DLLを構築し、SDKのPATHなしでSQLite／TLSプラグインをロードします。ネットワーク接続や永続DB・資格情報は使用せず、本体・SASL機構の機能試験を代替しません。

WinFlexBisonの反復構築と原本保持を確認する場合は、`verify_winflex_source_isolation.cmake`に`Qt6_ROOT`、`DEPENDENCY_ROOT`、`SMOOTHMAIL_SOURCE_ARCHIVES`、`CMAKE_MAKE_PROGRAM`を指定します。固定ソースから実ツールを構築する試験です。

### 生成済みライセンス資料

低水準の`tools/generate_licenses.py`でレビュー草案を生成した場合、Qt Release DLLをPATHへ追加して実行します。標準の[配布生成](distribution.md#生成コマンド)は草案ではなく配布用資料を生成するため、この草案専用試験とは区別してください。

```bat
cmake -DDRAFT=build/windows-licenses-review -DEVIDENCE=build/legal-review -DHELPER=build/package-tests/smoothmail_package.exe -P tests/cmake/verify_windows_legal_draft.cmake
```

原文SHA256、相対リンク、英日文書の対・manifest登録、未置換変数・余分な空白行の不存在、未承認／改名入力の拒否を確認します。最終表記には`-DFINAL_REVIEW=ON`を追加します。対応ソースの展開検査には`verify_source_review.cmake`を使い、テンプレート・`.env.example`の収録と実`.env`の非混入も確認してください。法的条件・翻訳の完全性・同意取得を認定する試験ではありません。

### HTTPS取得

ネットワーク不要の入力・ハッシュガードは`cmake -P tests/cmake/run_download_guards.cmake`です。制限付きWindows環境のTLS実通信試験は、次のように明示して実行します。固定されたビルドツールを取得し、CMake status 35の再現、OpenSSL取得、ハッシュ不一致・不正CAの拒否を確認するため、このエラーが再現する環境が前提です。

```bat
cmake "-DSMOOTHMAIL_DOWNLOAD_CURL=C:/path/to/Git/mingw64/bin/curl.exe" -P tests/cmake/run_download_tls.cmake
```

実Google認可・送受信などの手動プローブ（`tests/manual/`）は通常テストに含めません。実行には対象アカウントの本人認可と、通信・メール作成・資格情報保存それぞれの許可が必要です。

## DLL差し替えの検証

1. `tests/source-replacement/BuildReview.cmake`へ、新しい`REVIEW_ROOT`、対象`Qt6_ROOT`、配布した`SMOOTHMAIL_SOURCE_ARCHIVES`、構築済み依存の`REFERENCE_PREFIX`、内部Ninjaの`CMAKE_MAKE_PROGRAM`を`-D`で渡します。原本・有効パッチを検証し、識別用export関数だけを追加したQtSvg／KCodecsを別prefixへ構築します。既存Qt SDKは変更しません。
2. `verify-modified-source.ps1`で、改変が識別関数とその構築入力だけであることを照合します。
3. `prepare_package_review.py --help`の引数に従い、対象ZIPのSHA256を明示して検証用セットを作ります。同じ本体EXEに対して2 DLLだけを差し替え、共通の検証EXE・Qt Test DLL・offscreenプラグインを両構成へ追加します。元のZIPは変更しません。
4. 入力を読取り専用、ログ出力先だけを書込み可能にした**新規・外部ネットワーク無効のWindows Sandbox**で`run-package-guest.ps1`を実行します。ホストでは実行しません。通常版での識別関数の欠落、改変版での両関数の実呼出し、本体の起動・ロード元・ハッシュ、描画・SQLite・RFC2047等を確認します。
5. `result.json`と両構成の試験ログを回収し、失敗・スキップ・想定外の差分がないことを確認してからSandboxを終了します。検証用セットを製品として公開せず、差し替えたDLLの新しい配布には別途ライセンス・プライバシー監査を行います。
