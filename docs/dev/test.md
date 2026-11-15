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

CRTのimport選択ガードは `cmake -P tests/cmake/run_crt_guards.cmake` で単独実行できます。MSVC x64開発者環境では `cmake -P tests/cmake/run_crt_binary_guards.cmake` で、実再配布DLLのコピー一致・署名・版を確認し、改変コピー・未署名・版混在・不正/x86 PEの拒否を試験します。原本は変更せず、合成コピーは `build/` 以下に作ります。`VCToolsRedistDir` がある環境で配布補助をconfigureすると、これらもCTestへ登録されます。

ソースアーカイブの検証・非改変・パッチ適用と再利用のガードは `cmake -P tests/cmake/run_source_archive_guards.cmake` です。Git管理情報を作らず、ネットワークを使わずに12ケースと、Qt再構築先のSDK保護6ケースを確認します。配布補助のCTestにも含まれます。

ライセンス用`.env`とテンプレートの単体ガードは`cmake -P tests/cmake/run_license_template_guards.cmake`です。最終表記での草案表示除去を含む15ケースを、シェル評価や値の診断出力なしで確認します。`cmake -P tests/cmake/run_license_final_review_guards.cmake`では、実配置の改変・不足・余分なファイル・誤分類・重複・不正パスの6ケースを拒否することを確認します。いずれも配布補助CTestに含めます。

ライセンス文面案を原文収集結果とまとめた後は、QtのRelease DLLをPATHで参照できる環境で `cmake -DDRAFT=build/windows-licenses-review -DEVIDENCE=build/d05-legal-baseline-review -DHELPER=build/licenses-policy-tools/smoothmail_package.exe -P tests/cmake/verify_windows_legal_draft.cmake` を実行します。34ファイル／19コンポーネントの対応保持、`originals/`の原文SHA256一致、文面内の相対リンク、英語正本／日本語訳の対・常体・manifestへの登録、未展開変数の不存在、未承認・改名入力の拒否を確認します。配布補助の合成ZIP試験では、`licenses/manifest.json`の配置と旧`legal/`の非混入も検証します。法的条件や翻訳の完全性、利用者の同意を認定する試験ではありません。

最終表記の検査では`verify_windows_legal_draft.cmake`へ`-DFINAL_REVIEW=ON`を追加し、本文に草案・例示文言が残らないこと、アプリとCRTの版が適切に表示されることも確認します。source reviewの展開検査では、ライセンステンプレートと`.env.example`の収録、実`.env`の非混入も検査します。

実Google認可・送受信などの手動プローブ（`tests/manual/`）は通常テストに含めません。実行には対象アカウントの本人認可と、通信・メール作成・資格情報保存それぞれの許可が必要です。
