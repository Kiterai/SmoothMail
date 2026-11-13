# Windows SandboxのGUIチェック

## 同期・フラグ・一覧仕様803f145対応（最新）

最終記録は`build/behavior-check/output-verified/`です。11 EXEで**137 passed / 0 failed / 0 skipped**（init/cleanup含む）：connectiontester 21、mainwindow 12、accountrepository 11、guismoke 5、mimeregression 12、diagnostics 7、mimeparser 15、accountdialog 7、messageviewer 24、mailtransport 15、oauthclient 8。ホストCTest15対象（ネットワーク系3対象除外）・Python9件も成功しました。

フォルダごとの1時間境界/タイムゾーン、初回/新規フォルダ/UIDVALIDITY変更、範囲内外の削除、再起動した保留フラグ、送信拒否/再送、サーバ再取得優先、同期中の新しい編集の保護、バッジ部分クリック/自動既読を検証しています。スレッドはタイトル列で展開した子行の画面位置とクリック先を照合し、`screenshots/04-thread-view.png`でも確認しました。10034件の一覧・同期と旧KMime/KCodecs異常終了の回帰検査も維持しています。

実メールや実資格情報は共有していません。今回起動した4つのSandboxはすべて終了済みです。初回・中間の記録とは区別し、最終結果を参照してください。

## KCodecs修正後の追加検証（最新）

再発ログで特定したKCodecsのRFC 2047文字分類不具合を修正し、11 EXEで**133 passed / 0 failed / 0 skipped**（init/cleanup含む）。mimeregressionは12件（1152入力と日本語/encoded-wordの復号検査）、他は前回と同じ121件です。10034件同期の5901件目に非ASCII Subjectを加えて同期・キャッシュ再利用・途中切断回復を検証しました。ホストCTest15対象（ネットワーク系3対象除外）とPython9件も成功しています。記録は`build/codecs-check/output/`、変更前の意図的な2ケースの異常終了は`build/codecs-check/baseline/`に分離しています。実メール・実資格情報は使わず、この追加調査のSandboxは両方とも終了済みです。詳細は`crash-investigation-20261113.md`を参照してください。

## CRT異常終了対策後の追加検証

最新記録は`build/crash-check/fixed-output/`です。修正KMimeを正規のMSVC Debug設定で構築し、ネットワーク無効のSandboxで11 EXE、**128 passed / 0 failed / 0 skipped**（init/cleanup含む）を確認しました。diagnostics 7、mimeregression 7、mimeparser 15、mainwindow 11、accountdialog 7、messageviewer 24、accountrepository 10、connectiontester 19、mailtransport 15、oauthclient 8、guismoke 5です。高位バイト640入力、不正Dateを5901件目に置いた10034件同期、CRT/Qt Fatalの秘密非記録を含みます。ホストCTestはネットワーク系3対象を除く15対象、Pythonは配置3件・receipt6件が成功しました。ホスト側の成功はSandbox専用ケースの実行を意味しません。

変更前の3ケースの意図的な異常終了は`build/crash-check/baseline/`に分離しています。実メール・実アカウントは使っていません。この調査で起動したSandboxはすべて終了済みです。原因の確定範囲は`crash-investigation-20261113.md`を参照してください。

## 大量メール・ページ表示の追加検証

最新記録は`build/paging-check/output-round2/`です。ネットワーク無効のWindows Sandboxのループバック合成サーバーで、1万34件の初回同期・キャッシュ本文再利用・途中切断時の旧公開保持と、15 MiB級9通（原文合計135 MiB超）の同期を確認しました。本文の取得窓は最大100件、通常8 MiB単位です。

9 EXEで**104 passed / 0 failed / 0 skipped**（init/cleanup含む）。mainwindow 11、accountdialog 7、messageviewer 24、accountrepository 10、connectiontester 19、mailtransport 15、oauthclient 8、diagnostics 5、guismoke 5です。最後に追加したv1キャッシュ移行テストを含むaccountrepositoryは単独で再実行しています。ホスト非ネットワークCTest14対象とPython9テストも成功しました。

ネイティブGUIでは1万34件を100件/ページで表示し、101ページ目の34件、フォルダ全体からの本文検索、0件/1件での入力無効、重要/未読のサーバ反映と拒否時の非変更、未読集計、最新クエリ優先、ページ外の元メールへの移動、同期中のGUIタイマー継続を確認しました。スクリーンショットは`screenshots/11-large-mailbox-last-page.png`と`12-large-mailbox-filter-and-flags.png`です。

`memory-samples.txt`にはguest内のテストプロセスのWorking Set/Peak Working Set/Private Bytesを採取しています。合成サーバーとfixture、135 MiB級データを保持するテスト用インメモリSQLiteも含む値であり、製品のメモリ使用保証値ではありません。設計上の保持単位・残る制約・検索/スレッドのページ境界規則は`large-mailboxes.md`を参照してください。

初回の単通取得による検証は往復回数が多かったため中断してguestを破棄し、本文のサイズ制限付き一括取得へ改善してから上記を再検証しました。実アカウントや認証登録JSONはどのguestにも共有していません。

SDKなし・WindowsのみのPATHで通常アプリの空状態起動とトレイ終了も確認し、`normal-app-diagnostics/`に正常終了ログを回収しました。この作業で起動した2つのSandboxは両方とも終了済みです。

## UI仕様変更後の追加確認

新しい実行記録は`build/ui-update-check/output/`です。従来ワークフローに加え、右クリック経由のアカウント編集、空の仮名フォルダ作成後の自動インライン入力、子フォルダ作成、改名、削除警告での取消/承認、アカウント削除とサーバメール不変を確認します。設定更新が失敗した際に既存のOS秘密情報が復元されるケースもguestだけでテストします。

`tst_diagnostics`でWindows例外/C++terminateの出力を回収し、子プロセス強制停止からの次回起動検出も確認します。これは利用者の実メール同期クラッシュの再現ではありません。`tst_connectiontester`にはフォルダ操作の拒否、同期件数/単体サイズの安全上限、本文取得中の切断→再試行も追加しています。

追加後のguest結果は8 EXEで**90 passed / 0 failed / 0 skipped**（init/cleanup含む）。mainwindow 10、accountdialog 7、messageviewer 24、connectiontester 17、mailtransport 15、oauthclient 8、diagnostics 5、guismoke 4です。ホストの非ネットワークCTest14対象とPython9テストも成功しています。ホストでは意図的クラッシュ2ケースを実行せず、guestで実行します。Microsoft実アカウント、Googleの実GUI認可、Release配布設定は引き続き対象外です。

最新`build/local-run`だけを共有した別の新規guestでも、空白入り`C:\SmoothMail Local`・WindowsのみのPATHで通常起動を確認しました。トレイの「終了」後の診断を`build/ui-update-check/output/normal-app-diagnostics/`へ回収し、`session-start`/`session-clean-exit`と`.active`が残らないことを確認しました。両guestは確認後に終了済みです。

## 今回の範囲

利用者の指定に従い、ネットワーク無効のWindows Sandboxで通常GUIとループバック合成サーバーを使用した操作を確認します。Microsoftの実アカウント、GUIでのGoogle認可、配布用Release設定は今回の対象外です。実アカウントの登録JSON・パスワード・tokenはguestへ共有しません。

`SMOOTHMAIL_BUILD_GUI_SMOKE=ON`で専用の`tst_guismoke`をビルドできます。通常アプリへ合成データを混入させず、実`ApplicationController`・保存層・メール層・ウィジェットを別のテストEXEで駆動します。TLS証明書の信頼追加はテストプロセス内のみです。ダミー資格情報とDownloads保存を伴うため、CTestへは自動登録せず、使い捨てguestで`SMOOTHMAIL_GUI_SANDBOX_TEST=1`を指定した場合だけ実行します。ホストでこのopt-inを設定しないでください。

## 確認する操作

- アカウントなしでの通常起動と、操作可能/不可能なメニュー項目。
- アカウント追加の必須入力エラー、パスワードマスク、SSL/TLS既定、合成サーバー接続テスト、保存後の一覧同期。
- 日本語件名、差出人、本文、添付、ヘッダの折り畳み、スレッド表示。
- 列見出しのクリックによる昇順/降順切替、更新後の並び順維持、30秒自動更新。
- 返信先と返信参照ヘッダの初期値、新規作成のundo/redo、合成SMTP送信、合成IMAP下書き保存。
- 添付クリックによるguest Downloads保存、バイナリ照合、履歴の表示と元メッセージ移動。
- 通常アプリの閉じる操作によるトレイ常駐、ダブルクリック復帰、トレイメニューからの終了。

## 記録の場所

- Qt Test結果: `build/gui-check/output/tst_*.txt`
- ネイティブ画面キャプチャ: `build/gui-check/output/screenshots/*.png`
- `tst_guismoke`はOSの実画面を撮影します。`QT_QPA_PLATFORM=windows`を検査し、offscreen実行を成功扱いしません。
- GUI単体テスト群は`tst_mainwindow`・`tst_accountdialog`・`tst_messageviewer`です。

今回の実画面確認で、メイン/作成画面の初期サイズによる下端切れと、Qtの自動トグルに重ねてソート状態を切り替える問題を検出しました。初期サイズを画面内へ制限し、ソート状態を独立管理して更新時も維持する修正と回帰テストを追加しています。

## 結果

修正後のネイティブGUIは4テストEXEで**42 passed / 0 failed / 0 skipped**（init/cleanup含む）でした。内訳はmainwindow 9、accountdialog 6、messageviewer 24、guismoke 3です。統合ワークフロー内で列クリック・30秒タイマー・実ウィジェットの編集/保存を確認しています。ホストの非ネットワークCTest13ターゲットと、staging/receiptのPython9テストも成功しました。

合成データを使わない通常アプリでも、空状態のメニュー、トレイ常駐・復帰・終了を手動確認しました。チェック後に作成した最新の手元用ランタイムについても、別の新しいSDK未導入guestで空白入りパスからの起動とDLL探索を確認しました。Microsoft実認証、Google実認証のGUI、配布用設定は実行していません。
