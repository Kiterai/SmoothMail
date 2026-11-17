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

## 第2段階：既存UIDはFLAGS、新規UIDだけヘッダ

- Recent候補を最大1000件ずつUID FETCH FLAGSし、DB再利用は100件単位のまま。
  キャッシュ済みDateで過大なSENTSINCE候補を除外し、新規UIDだけ100件ずつ
  Dateとサイズをヘッダで確認する。本文の安全制限・最終再検証は維持する。
- `recentBoundary`はDate索引とidentity主キーの端点を読む3副問合せへ変更。
  JSONを全行評価する集計を避け、混在UIDVALIDITYの拒否も維持する。
- 追加transport試験は2005件の疎なUIDに3件追加する。
  既存ヘッダ0／新規ヘッダ3／本文3、FLAGS上限1000、フラグ更新、
  後半応答欠落後のステージ非公開・再試行、範囲外Date、サイズ超過拒否を検査する。
- 第2段階初回のGUI試験が1件失敗。同期停止用fixtureがHEADERだけを止めており、
  新しいFLAGS経路を止められなかった。待機を単に削除せず、
  fixtureの障壁を`pauseMetadata`としてHEADER/FLAGS双方へ適用。
  更新中の送信／アカウント編集・削除を従来どおり停止中に検査する。
  失敗時の性能試験はrunnerが開始せず、ログを`output/stage2`へ保持した。
- 修正後`output/stage2b`: repository29、transport42、mailtransport15、GUI9 =
  **95 passed / 0 failed / 0 skipped**。ホスト14対象も成功。
- 新規／既存ヘッダとFLAGSの件数も性能JSONへ記録し、validate.pyで照合する。
  stage1、stage2bの各120同期と各24 DBが検証成功。
- 同じguestで元の保存済みEXEを再実行したbefore中央値（10通追加Recent）:
  5000×2KiB 2347ms、1000×64KiB 1664ms、履歴5000件459ms。
  第1段階は593/136/55ms、第2段階は624/218/47ms。
  FLAGSは転送量／往復数を減らすが、KIMAP側のJob処理を含むローカル実測では
  どの条件でもヘッダ方式よりCPU時間が短くなるわけではなかった。
- FLAGS窓100/250/500/1000を各45同期（3反復）で比較し、180同期すべて成功。
  この中では1000が最良。5000件10通追加の中央値はそれぞれ1062/969/979/562ms。
  20ms遅延・1000件では616/492/468/448ms。
  小さい窓へ単純に戻すことは採用しない。窓の切替環境変数は計測用ソースコピーにしか
  挿入せず、製品にチューニング用環境変数を追加していない。

## 第3段階：共通メタデータの重複公開と永続書込みを抑制

- 全フォルダLISTの公開は最初の成功フォルダだけ。以後はそのフォルダの差分公開とする。
  フォルダがF件ある際に全F件の属性を書き直す処理をF回行わない。
- 属性・未読数・UIDVALIDITYが同じならcache_folders/folder_stateも更新しない。
  消えたフォルダだけを削除する。未読集計に後方互換の部分索引を追加し、
  既存v5 DBに索引を追加しても原文・user_versionが変わらないことを試験する。
- repositoryのSQL trigger監視は本文だけでなくフォルダ属性とfolder_stateにも拡張。
  変更なし全同期で、これらすべての永続行の書込み0件を確認。
- AllTests runner初版はCTestのoffscreen指定を反映せず、aboutdialogの画面高さ制約と
  mainwindowの描画テスト4行が失敗した。製品UIや期待値を変えず、
  既存CTestと同じ4テストだけoffscreen、GUI統合はwindowsで実行するよう修正した。
  元の失敗ログは`output/stage3`へ保持。
- 修正後`output/stage3b`の17 EXEを独立集計し
  **276 passed / 0 failed / 0 skipped**。基礎120同期と24 DBも成功。
  10通追加Recentの履歴5000件は38ms（before459ms）、
  遅延なし30フォルダは228ms（before292ms）。
