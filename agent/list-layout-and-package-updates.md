# 2026-11-16 一覧UI・配布構成の仕様反映

- 対象は仕様コミット`033f2a1`〜`cf71247`。文言整理以外の5項目を実装。
- Alt+左右は一覧の表示行位置を次ページへ引継ぎ、短いページでは末尾へ補正する。従来の上下キー境界移動も維持。
- 全カラムをInteractive／movableに変更。更新時の自動幅調整を廃止。`MainWindow::messageColumns()`／`restoreMessageColumns()`でQt header stateを入出力し、controllerが`QSettings`の`display/messageColumns`へ保存する。UI単体では実ユーザ設定を読み書きしない。
- フィルタ・ソート変更は選択メールのUID／UIDVALIDITY／storageFolderを既存のreveal検索へ渡し、DBが該当ページを求める。再構築時に選択行の祖先を展開してスクロールする。フィルタから外れたメールは選択解除し、履歴ジャンプ用の「見つからない」通知を出さない。
- 添付一覧を`attachmentHeaderSplitter`の上側へ移し、高さ110の上限を撤廃。下側にはヘッダ開閉行と従来の`headerBodySplitter`を置く。添付がないとQtが境界も非表示にする。
- ZIP化時にステージ相対パスを配布用パスへ変換する。DLLカバレッジと既存ポリシーはruntime相対のまま検証し、ZIP内のハッシュ一覧は実際の配布パスを記録。通知は`license/`、本体は`runtime/Smoothmail.exe`、`README.md`は対象ソースの`docs/usage.md`と同一バイト。`launch.cmd`は自身の場所を基準に本体を起動する。
- バイナリ監査はステージ直下と展開後の`runtime/`に対応し、同じハッシュ・範囲・作業者パス拒否を維持。

## 検証

- `build/spec-changes-check.cmd`で既存Release依存を再利用して本体・テストを構築。`build/spec-changes-test.cmd`はMSVC環境を読み込んでCTest全23ターゲット成功。最初のMSVC環境なし実行ではCRT用2テストがnmake不在で失敗し、正しい環境で再実行して解消。
- `build/ui-spec-check.cmd`で従来のビルド先も更新し、`build/ui-spec-test.cmd`のホスト向け12ターゲットも成功（通信3ターゲットは従来どおり対象外）。OS資格情報の書込みはopt-inしていない。
- package guardsは合成ZIPを独立展開し、構成・README内容・ランチャー相対パス・全SHA256・既存ZIP非上書き・決定性を検査した。今回、実配布用ZIPの生成やSDKなしguestでの新ランチャー実起動は行っていない。GUI検証はoffscreenであり実画面の外観確認ではない。
