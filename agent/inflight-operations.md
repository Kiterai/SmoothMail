# 481eb90：取得中の操作

## 同期・アカウント

- 情報取得用MailServiceと、送信/下書き/接続テスト用MailServiceを分離。後者同士の同時操作は従来どおり1件。同期キューは補助サービスの待機で止めない。
- 同期開始時のAccountと取得済みsecretは値として保持。OKによる保存後も既存同期を停止・再設定しない。編集だけでrefreshを自動発行しない。
- IMAP設定変更はcache_resetへ記録。旧設定の同期は旧キャッシュを使って完了可能。保存済みの新IMAP設定で次回同期を開始するときに、旧接続先のキャッシュ・保留操作を破棄する。他サーバの同じUIDを再利用/更新しないための保護。
- 削除要求時はUI/実行待ちキューから即時除去。認証待ちコールバックのserialを無効化し、対象同期のatomic cancel flagとOAuth取得をキャンセルする。KJobの開始前と待機タイマー（100ms）で検査。QThreadの恒久interruptionとは分け、次のアカウント/再試行で再利用可能にする。
- 削除したIDの遅延同期/cacheLoadedは表示しない。SQLite公開もアカウントの存在を検査する。処理中SQLやサーバに送信済みの操作を強制ロールバックするものではない。
- CredentialStore内のジョブを発行順に直列化。AuthenticationBrokerの世代を無効化した旧OAuth応答は、進行中の旧接続には返しても新しい保存済み資格情報へ書き戻さない。アカウント削除は別のcancel世代で、遅れたkeychain readからOAuthを開始することも防ぐ。

## フォルダ操作（SQLite v4）

- pending_foldersはrevision順のCREATE/RENAME/DELETEジャーナル。ローカルmetadataに重ねて表示する。サーバの実フォルダ名・メッセージ本体のキャッシュは同期前には動かさない。
- loadPage/loadMessage/queueFlagはローカル名からジャーナルを逆適用して元のサーバパスへ解決する。未送信の新規フォルダは空。既存フォルダの改名中も本文を読める。通常GUIのloadCacheはmetadataOnlyを使用する。
- 同期開始時にジャーナルの上限revisionを固定し、最大100件ずつ、LIST確認と操作・再LIST・ローカル確定を順に行う。その後で各フォルダの保留フラグを反映する。同期中の追加操作はoverlayとして残す。
- 成功を確認したRENAMEだけ、キャッシュ/保留フラグのパスを移す。応答喪失後の再試行で元がなく変更先だけある場合や競合では、別メールボックスの可能性があるので元のUIDキャッシュ/保留フラグを捨てて再取得する。
- サーバに既に存在するCREATEや既にないDELETEは繰り返さない。競合時は実際のLISTを優先。タグ付き失敗/確認前の通信失敗ではジャーナルを残す。削除警告は「次回同期でサーバから削除」と明示する。
- 操作名/パスの検証、名前4096文字・階層64・保留10000件の保護。値はbindし、パスをSQLやログへ埋め込まない。

## 削除直後の終了競合

SQLite workerのremove/update等は、最後にreloadを自スレッドへキューする。GUIが先にState破棄のBlockingQueued barrierを入れると、reloadがその後へ入りnull Stateを参照できた。reloadの投稿前/実行時にinterruptionとStateを検査する。20回の削除/即時破棄で回帰検査。

## 検証の範囲

- Repository：ジャーナル再起動、改名中の本文/一覧/フラグ、成功後のパス移行、設定編集後も旧同期可能・新設定開始時のリセット。
- Transport：停止したヘッダ要求を3秒未満でキャンセル・同一workerで再試行、フォルダ失敗/再送、フォルダ改名の後にフラグを送信。
- GUI：停止した取得中にローカル作成/SMTP送信/アカウントOK、旧取得完了まで旧設定、次回は新サーバ、次の取得を止めてローカルアカウント削除。既存のページ/閲覧/編集フローも維持。
- OS keychain：書込2件→読込→削除→読込を待たずに投入し順序保証を確認。実資格情報ではなく使い捨てguest内のdummyだけを使う。
- 調査用の最初のGUI異常終了と符号化したコードフレームは`build/inflight-check/gui-debug/`。最終結果とは区別する。テストcmdは負の例外exit codeも失敗扱いにし、集計では全EXEのTotals存在を必須とする。

## 最終成果物

- 最終Sandbox12 EXEは149 passed / 0 failed / 0 skipped。全Totalsと失敗一覧が空であることを検証。`build/inflight-check/output-final/`を参照。ホスト15対象/Python9件も成功。
- `build/local-run-inflight-final`をハッシュ照合後に`build/local-run`へ移動済み。旧版は`build/local-run-before-inflight`。通常ランチャー変更なし。配置ログは`build/inflight-final-stage.log`、EXE/PDBは`build/inflight-check/symbols/`。
- SDK・実DB・実アカウントには接続/変更していない。今回の3 Sandboxはすべて終了済み。初回の負exit codeによるGUI異常終了は失敗の再現記録であり、最終成功記録ではない。
