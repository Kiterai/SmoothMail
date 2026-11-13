# 0a98815までの仕様追従

- メール一覧はMessageTreeView::scrollToで横位置を保存・復元し、Qtの幅広セルへの自動追従だけを抑止。縦方向の表示位置移動とユーザーの横スクロールは残す。クリック、上下キー、明示scrollToを横位置0/非0で回帰検査。
- OAuth認可専用ボタンを削除。ServerFields内のgrant/clientIdは値として保持する。接続テストは入力検証（登録情報不要）→必要なSMTP/IMAP認可→登録情報込み検証→既存transient/token取得・両プロトコル接続テストを行う。
- テスト成功まで決定不可。入力・認証・資格情報の変更で成功を無効化する。OAuth待機中のみキャンセル可。取消後の遅延OAuth成功やテスト完了では保存を有効にしない。保存済みアカウントも編集ダイアログごとに再テストが必要。
- 接続テストの太字・赤い「未」／緑の「✓」はQPushButton派生の描画。外部アセットなし。ボタンのtextも同じ平文でアクセシビリティ・テストから参照可能。
- 配布はtools/package-windows.py。依存取得・ビルドはしない。独立したRelease専用stageを追加し、旧Debug stageのガードは維持。legal manifestによる全DLLの通知対応付け、ASCII/UTF16のパス検査、Debug/DB/log/PDB拒否、固定ZIPメタデータ、相対ハッシュ一覧を生成。
- Release依存prefixがこの環境にないため、実Release ZIP／Releaseクリーン起動検証はブロック中。Debugで偽の配布ZIPは作らない。配布用legal入力の人手による準備と監査も必要。手順はdocs/dev/distribution.md。

## 最終検証

- オフラインSandbox12 EXE：162 passed / 0 failed / 0 skipped。全Totalsとfailures.txtの空を確認。`build/account-update-check/output/`。今回起動したSandbox1台は終了済み。
- ホスト非ネットワークCTest15対象成功。Python unittest discover31件成功（Release guard・機械パスASCII/UTF16・x64 PE検査・legal対応付け・既存成果物保護・ZIP構造とSHA256等）。実OAuth画面のライブ認可は未実施。
- パッケージ用CLIを実環境でも起動し、Release KPim6IMAP.dllの不足で公開前に停止することを確認。Release DLLの一式とlegal入力がないため、実アプリReleaseのZIPは存在しない。mock/fixtureのZIP単体テストとは区別する。
- `build/account-update-check/runtime`の42ファイルのハッシュと、ビルド/配備/シンボル用EXE同一性を確認し、通常起動先`build/local-run`を更新。旧版は`build/local-run-before-account-update`。使用中プロセスの強制終了はしていない。
