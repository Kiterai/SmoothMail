# テスト

[Debugビルド](build.md)後、リポジトリルートのMSVC x64開発者コマンドプロンプトで実行します。以下は標準の依存rootを使う例です。別rootの場合はパスを読み替えてください。

## 通常の自動テスト

```bat
set PATH=C:\Qt\6.12.0\msvc2022_64\bin;%CD%\build\deps\install\Debug\bin;%CD%\build\deps\native\gnu-runtime-v1\x64-windows\debug\bin;%CD%\build\deps\native\x64-windows\tools\gettext\bin;%PATH%
ctest --test-dir build/windows/Debug --output-on-failure
```

GUI単体テストはoffscreenで動作します。OS資格情報への書込みや意図的クラッシュを伴うケースは、通常環境ではスキップします。成功件数だけでなくスキップ内容も確認してください。個別実行は `ctest --test-dir build/windows/Debug -R "^mainwindow$" --output-on-failure` のように指定します。

## 依存・通信の追加テスト

同じビルド先へオプションを追加して再構築します。

```bat
cmake -S . -B build/windows/Debug -DSMOOTHMAIL_TEST_SASL=ON -DSMOOTHMAIL_TEST_KDE_BASE=ON -DSMOOTHMAIL_TEST_KIO=ON -DSMOOTHMAIL_TEST_MAIL=ON -DSMOOTHMAIL_TEST_NATIVE=ON
cmake --build build/windows/Debug --parallel 4
ctest --test-dir build/windows/Debug --output-on-failure
```

SASL機構、KDE API、GNU DLL再配置、合成IMAP／SMTP・TLS・OAuthを検査します。通信系はループバックTCPが必要です。実サービスの認証や実メールは使いません。制限されたシェルで接続が拒否される場合は、テストを成功扱いせず、ループバックの使える隔離環境で実行してください。

## Windows SandboxでのGUI・資格情報テスト

1. 上記に `-DSMOOTHMAIL_BUILD_GUI_SMOKE=ON` を追加して構築します。`tst_guismoke` はCTestには登録されません。
2. [Debugの配置手順](run.md#デバッグ実行)で実行用フォルダを作り、テストEXE・対応するQt Test DLL・SDKの `plugins/platforms/qoffscreend.dll`（offscreenテスト用）を追加した**専用テストセット**を用意します。通常起動フォルダを上書きしません。
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
| `run_license_template_guards.cmake` | `.env`入力・変数置換・空白行整理 |
| `run_license_final_review_guards.cmake` | 配置ファイルの不足・改変・誤分類・不正パス |

CRT実バイナリの試験はMSVC x64開発者環境で行います。原本は変更せず、合成コピーは`build/`以下へ作成します。

WinFlexBisonの反復構築と原本保持を確認する場合は、`verify_winflex_source_isolation.cmake`に`Qt6_ROOT`、`DEPENDENCY_ROOT`、`SMOOTHMAIL_SOURCE_ARCHIVES`、`CMAKE_MAKE_PROGRAM`を指定します。固定ソースから実ツールを構築する試験です。

### 生成済みライセンス資料

[文面生成](distribution.md#文面の生成と確認)後、Qt Release DLLをPATHへ追加して実行します。

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
