# 803f145追従：同期とローカル編集

## 実装の不変条件

- `MailService::SyncMode::SelectedFolder`は既存のテスト/互換呼出し用。通常コントローラーはRecent、Ctrl+F5はAllを使用する。どちらもLISTし、全selectableフォルダを巡回する。
- Recentはcache_indexの有効Date最大値−3600秒を下限にする。UIDVALIDITY不一致/日付なしはfullへフォールバックする。SENTSINCEは日単位でtimezoneを無視するので前日から検索し、100件単位のヘッダ取得後に絶対時刻を比較する。本文取得は境界以降だけ。
- 差分ステージには旧キャッシュをSQLでコピーし、境界以降だけを置換する。範囲外の削除は全同期まで反映されない。本文再利用、通常8MiB/最大100件、UIDVALIDITY/件数/UIDNEXT再検証を保持する。検索UID一覧はKIMAP SearchJobの結果として保持するため、UIDリスト自体は件数に比例する。
- 公開はフォルダ単位のトランザクション。全アカウントを1トランザクションにはしない。途中失敗しても既に成功したフォルダ/フラグreadbackは保存される。
- SQLite v3のpending_flagsは(account,folder,validity,uid,mask)で最終操作を集約。queueFlagはDB上の実際のflagsから単一bitだけ変更し、未読数とoutboxを同じtransactionに保存する。GUI上では先に即時反映する。
- 送信開始revisionを固定し、最大100件ずつ読み、mask/値の4グループへUID STORE。その後UID FETCH FLAGSで再取得しサーバ値を採用する。Store成功だけでoutboxを消さない。存在しないUIDは再取得の不在を確認してローカルから除去する。
- acknowledgementは送信したrevisionだけ削除。残る保留bitをサーバ値へ重ねる。取得完了時も最新outboxをステージへ重ね、同期中の新しい操作を失わない。UIDVALIDITY変更では旧UIDへSTOREしない。
- アカウント編集/削除ではoutboxもクリアする。秘密情報はoutboxに保存しない。失敗したネットワーク操作は保留を残す。
- コントローラーはアカウント単位に同期要求を集約する。待機中のRecentはAllへ昇格可能。実行中Recentに対するAll要求は次の要求として残す。フラグ編集自体はネットワーク待ち/同期中も受付ける。

## UI

- バッジは16px中央描画、重要/未読はオフ状態も輪郭記号を表示して反転可能にする。クリック判定を同じ16px領域へ限定する。記号は既存の星/丸で、外部アセット追加なし。
- QTreeViewのtreePositionだけをタイトル列3へ移す。内部の子行は列0のQStandardItemが所有するQtの標準構造を保持する。タイトル列へ子行自体を移すと、モデルに子行があってもQTreeViewが表示できないため注意。ネイティブテストで展開した子行のvisualRectとindexAtを照合する。
- 復元のためのモデル再構築では自動既読を発火しない。本文ロード中のローカル編集を、遅れて到着した本文のflagsで上書きしない。ページ再取得時は選択中本文のflagsも更新する。

## 検証

- `tst_connectiontester`: 全フォルダ差分/全件、独立したフォルダ日時、1時間境界とtimezone、範囲内外の削除、新規フォルダ、validity変更、送信拒否/再送、まとめたSTORE、FLAGS再取得優先、対象削除。
- `tst_accountrepository`: 再起動した保留、異なるbitの共存、古いackと新revision、stage公開中の編集、validity変更時の保留除去。
- `tst_mainwindow`: アクション名/ショートカット、バッジ領域外/中心、自動既読のon/off、タイトル列の枝/子行自動既読。
- `tst_guismoke`: ネイティブGUIの一連操作、10034件ページ/検索、クリック時にサーバへ送らないこと、明示同期での失敗/再送。
- 実アカウント・メールDB・認証登録JSONをテストへ持ち込まない。実行記録は`build/behavior-check/`。

## 最終成果物

- 11 EXEの137 passed / 0 failed / 0 skippedは`build/behavior-check/output-verified/`。ネイティブ子行の可視性検査を含む最終ビルドです。ホスト15対象とPython9件も成功。
- 配置42ファイルのSHA-256とbuild/dev/smoothmail.exe、保存したEXE/PDBとの一致を確認。新版は`build/local-run-behavior-deliver`、配置ログは`build/behavior-deliver-stage.log`、シンボルは`build/behavior-check/symbols/`。
- `build/local-run`の移動は別プロセスによる使用中エラーで失敗。通常ランチャーは意図的に旧版のまま。ユーザープロセスは終了させていない。新版を試す前に旧アプリのトレイ終了が必要。存在しない`local-run-before-behavior`をバックアップとして案内しない。
- 今回の4 Sandboxはすべて終了。初回成功・中間モデルテスト失敗・ネイティブ画面点検を経て、最後に全11 EXEを再実行した結果が最終ログ。
