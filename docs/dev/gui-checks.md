# Windows SandboxのGUIチェック

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
