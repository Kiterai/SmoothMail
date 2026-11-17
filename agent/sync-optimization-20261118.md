# 差分同期の高速化（2026-11-18）

## 第1段階：参照ステージとバッチ再利用

- 調査`a0e96e9`を受け、既存raw/indexをTEMPへ全コピーする方式を廃止。
  `sync_messages/sync_index`は新規本文だけ、`sync_reused`はUIDとflagsだけを保持する。
- `reuseMessages`は最大100件を1回のBlockingQueuedConnectionと1 transactionで処理。
  prepared queryを再利用し、raw/indexの両方が存在することを確認する。
  旧`reuseMessage`は単件wrapperとして維持する。
- commitは範囲内の消失UIDのみ削除し、取得した行だけupsertする。
  不変本文と不変flagsはUPDATEもしない。保留flagsは従来どおり公開直前に適用する。
  日付境界、UIDVALIDITY変更、空フォルダ、アカウント別stage、公開rollbackを維持。
- 新規repository試験はSQLite triggerで本文行のINSERT/DELETE/UPDATEを監視。
  1件追加＋1件消失だけを書き、次の変更なし全同期では本文行への書込み0件であることを確認する。
  範囲外フラグ保持、未日付行保持、同期中のローカル編集、重複／過大バッチ拒否、
  公開失敗時のフラグ非公開も検査する。
- 別Release構築先`build/sync-opt/app`と、計時コピー用`build/sync-opt/perf`を使用。
  既存の製品配置やSDKは上書きしない。ホスト非通信CTest14対象成功、
  Python52件（8 skip）成功。ネットワーク無効guestでもrepository29、transport41、
  mailtransport15ケース成功（失敗・skipなし）。GUI統合と性能比較は継続確認する。
