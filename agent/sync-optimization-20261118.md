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

## 第4段階：大量FLAGS応答を2本の標準検索で置換

- FLAGS窓の調整だけでは、KIMAPの大量応答解析が大きく残った。
  Date候補が256件を超える場合、同じSENTSINCE条件とNOT SEEN／FLAGGEDの検索を
  各1回実行する方式を追加。対応拡張は不要で、Seen/Flaggedは従来どおり両方を確認する。
- 整列済みのコンパクトなUID配列と二分探索を使う。
  元の候補に含まれないUID、重複UID、無効UIDは失敗にし、Dateの厳密境界もDB側で維持。
  本文は未知UIDだけ取得する。UID一覧以外のステージングは引き続き件数／byte上限付き。
- 標準検索を拒否したサーバーはFLAGSへfallbackする。
  タイムアウト・キャンセル・切断・不正UID結果ではfallbackしない。
  KIMAPが不正tagged応答をerrorTextのみで返す場合も、空の未読集合と誤解しないよう拒否する。
- 205件のFLAGS経路と2005件の検索経路を同じ追加／失敗／Date／サイズ試験で検査。
  検索側は対象外UID、重複UID、不正tagged応答、明示拒否時fallbackも検査する。
- 初回のfallback試験が失敗し、SELECT後のSession状態がAuthenticatedではなくSelectedである
  ことを確認。認証済み／選択済みの双方を許容するよう修正した。
  不正応答を成功扱いしたのではなく、拒否後の正しいFETCH再試行を検査して修正している。
  targeted再試験4 passed / 0 failed / 0 skipped。元の全体ログは`output/stage4`に保持。

## 最終確認と改善幅

- `output/stage4b`の全17 EXEを独立集計:
  **277 passed / 0 failed / 0 skipped**（init/cleanupを含む）。
  GUI統合は実widgets、CTest指定の4 UI単体はoffscreen。
- 同じVMで保存済み旧EXEを再実行したbeforeと比較。
  最終版は基礎120同期＋TLS/GUI詳細60同期が全成功。
  基礎24 DBのquick_check、raw/index件数、連続UID、取得件数、ヘッダ／フラグ経路も照合成功。
- 以下は**10通追加Recent、各3反復の中央値、ミリ秒**。既存配置や実メールを使用した測定ではない。

| 条件 | before | 最終版 | 比率 |
|---|---:|---:|---:|
| 1000件 × 2KiB | 475.8 | 52.7 | 9.0倍 |
| 5000件 × 2KiB（Date集中） | 2346.7 | 179.0 | 13.1倍 |
| 1000件 × 64KiB | 1664.1 | 77.8 | 21.4倍 |
| 履歴5000件・最近1件 | 459.1 | 37.6 | 12.2倍 |
| 1000件 × 2KiB・20ms遅延 | 827.5 | 295.1 | 2.8倍 |
| 30フォルダ・20ms遅延 | 2063.6 | 2080.6 | 約同等 |

- 5000件に10件追加した最終版は、UID SEARCH計3回（候補、未読、重要）と
  新規ヘッダ1 Job／10件、本文1 Job／10件。既存5000件のヘッダ・本文を再転送しない。
  MIME parseも10回。再利用は51バッチ。例ではSearch計27ms、FETCH計3ms、
  reuse計55ms、commit計77ms。残る大口はSQLite側のメタデータ照合・公開。
- ローカルTLSの1000件＋10件は詳細確認中央値76.4ms。
  詳細の100件ページ取得2.6–23.3ms、描画適用13.7–23.4ms。
  模擬サーバー・KIMAP・SQLite・OSキャッシュ等の条件に依存する値で、
  実サービスの絶対時間や全てのメール形態の倍率を保証しない。
- 未読COUNTのEXPLAIN QUERY PLANで`cache_messages_unread`の使用を確認。
  既存DBは初回オープン時に索引作成の一時的コストがあるが、データの再取得／リセットはしない。
- 30フォルダ・遅延ありではSTATUS30＋EXAMINE60の通信往復が依然中心。
  検証を省いて見かけだけ高速化する変更はしていない。
  接続再利用やLIST-STATUS等は別途互換性検証を要するため今回の実装には含めない。

## 証跡

- `build/sync-opt/share/output/stage1/{before,after}`、`stage2b/after`、
  `stage3b/{after,deep}`、`stage4b/{tests,after,deep}`。
  中間失敗の`stage2/stage3/stage4`も保持し、成功と扱っていない。
- `build/sync-opt/{before,stage1,stage2b,stage3b,stage4b}.json`が基礎の集計。
- FLAGS窓比較180同期を含む、今回の全段階の性能同期は合計900回。
  最終コードの成功判定はstage4bの180回と上記277テスト。
- 検証中に用いたSandboxはネットワーク無効の
  `f93beb5d-841d-4ced-84e8-e4169f05390a`。
  実アカウント／ホストのOS資格情報／実メールにはアクセスしていない。

## Release生成・起動先

- 製品ソースの最終コミットは`be90d05276d55f46f7c37d1ecca4cc9a8356fe7e`。
  標準Release構築で本体・通知・対応ソース・ZIP・検証済み展開物を生成。
  `SMOOTHMAIL_DISTRIBUTION=ON`、source manifestのSmoothMail commitも同じ値を確認。
- Safe Shellの別ユーザー実行により既存依存Gitの所有者検査で停止したため、
  `build/sync-opt/trusted-git.config`をこのビルドの子プロセスだけに指定した。
  元のユーザーglobal設定をincludeし、固定manifestのソース、該当するパッチコピー、
  固定Qt対応ソース2件の**具体的なパスだけ**をsafe.directoryに加えた。
  ユーザーのglobal Git設定、所有者、ACLは変更せず、SHA／汚染検査も維持した。
- ZIP検証後の通常配置`build/rel/run`→`run-previous-*`がWinError 5で拒否され、
  標準ワークフロー全体の終了コードは2。原因を使用中と断定せず、既存プロセスの
  停止・権限変更・無理な上書きは行っていない。
  最新`build/rel/artifacts`の生成は完了しているが、通常のrunは旧版のまま。
- 検証済み展開物`build/rel/jobs/4ecd0a8219a06aef/unpacked/SmoothMail`を
  新規の`build/sync-opt/run`へコピー。起動は**`build/sync-opt/run/launch.cmd`**。
  同梱manifestの360ファイルのSHA256と本体app EXEとの一致を再確認した。
- 新Release EXE SHA256:
  `e5faa55ca1d9f649d312c98ca7a8a6843d8251bfea79378a5224bdc4fbba120d`。
  旧`build/rel/run/runtime/Smoothmail.exe`は
  `0d8b6b6b82f3cedef4aaecfb7d9a0f900922ec6fa06ca43301a64c7348f6469d`のまま。
- 検証用8GiB Sandboxは回収後に終了成功。
  別の新規4GiB・外部通信無効Sandbox
  `5fa73695-ced0-47b8-94bc-8d9bdc66392c`で新Release一式だけを起動。
  ネイティブ空状態の正常表示、qsqliteロード、本体hash一致、
  ロードモジュール104件がguest配置先／Windows配下のみであることを確認。
  `output/release-{startup,modules,exe-sha256}.json`へ回収し、このSandboxも終了成功。
- 実メール、実サービス認証、最終配布の法的監査を完了したという意味ではない。
  配布物の`AUDIT-REQUIRED.txt`は維持する。

## 通常配置への反映（利用者による終了確認後）

- 利用者の「終了しました。通常配置に反映してください。」を受け、Smoothmailプロセスが
  検出されないこと、新旧実行先が通常のディレクトリであることを確認した。
- 検証済み`build/sync-opt/run`を一旦通常配置と同じ親へコピーし、
  manifestの360ファイルおよび全361ファイルの完全なinventoryを照合してから置換した。
  配置後も同じ照合に成功。本体hashは上記`e5faa55c…120d`でapp EXEと一致する。
- 旧版一式は`build/rel/run-previous-sync-opt-20261118-041947-1dc4feee`へ退避。
  アプリの強制終了、ユーザーDB・設定・OS資格情報の操作は行っていない。
- 現在の通常起動先 **`build/rel/run/launch.cmd`は高速化版**。
  上記の「通常のrunは旧版のまま」はアクセス拒否時点の記録であり、この反映で解消した。
- 証跡は`build/sync-opt/normal-deployment.json`。配置のみで、ホスト上ではアプリを起動していない。
