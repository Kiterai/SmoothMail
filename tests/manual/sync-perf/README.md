# 同期性能の隔離計測

Windows Sandbox専用の手動プローブ。製品の動作変更・既存ランタイムへの上書きはしない。
合成IMAPサーバーと本体のMailService／AccountRepository／MimeParserを同じプロセスで動かす。
サーバーは外部接続しないloopback専用のテスト実装で、実運用のメールサーバーではない。

## ビルド

既存のRelease依存を準備し、x64 Native Tools Command Promptから実行する。
Qt／依存／Ninjaのパスは自身の既存構築先に合わせる。

```bat
cmake -S tests/manual/sync-perf -B build/sync-perf/compile -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=<ninja.exe> ^
  -DQt6_ROOT=<Qt SDK> -DCMAKE_PREFIX_PATH=<依存install/Release> ^
  -DSMOOTHMAIL_NATIVE_ROOT=<native/gnu-runtime-v1> ^
  -DSMOOTHMAIL_GETTEXT_TOOLS_ROOT=<native>
cmake --build build/sync-perf/compile --target sync_perf
```

`instrument.py`は本体ソースのコピーをbuild配下に生成し、関数入口にRAIIタイマーを加える。
対象宣言が変わった場合は一致件数を検査して停止する。元ソース・依存原本は編集しない。
DB APIの時間はBlockingQueuedConnectionの往復を含む。`worker.*`はその内側で、足し合わせない。
IMAP Job時間は通信・サーバー・KIMAP処理を含み、純粋な通信時間ではない。
サーバー時間もJob時間と重なる。計時器自身のコストは控除しない。

## 実行・回収

1. `build/sync-perf/share/runtime/`に既存Releaseの**runtimeの内容だけ**をコピーし、
   `sync_perf.exe`を追加する。設定、登録JSON、ユーザDB、`.env`は共有しない。
2. `share/output/`を作り、`run-guest.ps1`をshare直下にコピーする。
3. ネットワーク無効、RAM 8192MiBの新規Windows Sandboxを起動。
   shareだけを読み書き可能で`C:\McpShares\share1`に共有する。
4. guestで以下を実行する。runtimeはguestのローカルCドライブへコピーして計測する。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File C:\McpShares\share1\run-guest.ps1
```

基礎計測は8条件×5操作×3反復を診断ログON/OFFで各120回。
全体更新、Recent更新、10通追加後のRecent、選択フォルダRecentを比較する。
メールは合成の2KiB／64KiBプレインテキスト、フォルダ追加分は空。
20ms条件は**コマンドを受けてから処理するまでのサーバー側タイマー遅延**であり、
実ネットワークのRTT・帯域制限の再現ではない。

詳細確認には同じEXEをshare直下に`sync_perf_deep.exe`として配置し、
`run-deep-guest.ps1`もコピーして実行する（基礎計測完了後のみ）。
4条件×5操作×3反復、計60回。DBworker内外の計時に加え、
テストCAだけをプロセス内で信頼するTLS接続、100件ページの読出し・本体MainWindow描画を確認する。
OS証明書ストアや実資格情報は変更しない。
詳細確認はController全体・OAuth・本文表示のE2E計測ではない。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File C:\McpShares\share1\run-deep-guest.ps1
```

基礎計測の回収には合成SQLiteも含むため、十分な空き容量が必要。
ログ／JSON／画像の回収を確認してから、起動したSandboxを必ず終了する。
ホストでEXEを実行しない。`SMOOTHMAIL_PERF_GUEST=1`は明示opt-inであり、
仮想環境であることを技術的に保証する検出器ではない。

```bat
python tests/manual/sync-perf/validate.py build/sync-perf/share/output/with-diagnostics
python tests/manual/sync-perf/validate.py build/sync-perf/share/output/without-diagnostics
python tests/manual/sync-perf/summarize.py build/sync-perf/share/output/with-diagnostics/results.jsonl build/sync-perf/summary-with.json
```

`validate.py`は基礎計測の120結果、本文取得件数、24 DBのquick_check・件数・UID・索引を読み取り専用で検査する。
集計は各条件3回の中央値と最小／最大。ON→OFFの固定順なので、差をすべてログ負荷とは断定しない。

## 最適化の段階比較

`run-optimization-guest.ps1 -Phase <新しい名前> [-Baseline]`は、上と同じ共有配置から
repository／transport／mailtransport／GUI統合試験を実行し、成功した場合だけ120同期を計測する。
`-Baseline`時は共有runtimeに別途置いた旧`sync_perf_before.exe`も先に実行する。
段階ごとに新しいguestローカルフォルダ／DBを使用する。複数の計測を同時実行しない。
`-AllTests`は配置した全テストEXEを実行する（CTest指定の4 UI単体試験はoffscreen）。
`-Deep`は基礎計測後にTLS／ページ／描画の60同期も追加する。

`run-window-guest.ps1`は計測用コピーのFLAGS窓を100/250/500/1000へ切り替えて比較する。
各45同期、既定1000。`SMOOTHMAIL_PERF_FLAG_WINDOW`による切替は`instrument.py`が
コピーへ挿入するもので、製品ソース／通常EXEには存在しない。
この比較時だけ大候補向けの2本のフラグ検索を無効化し、FLAGS経路を強制する。
通常の基礎／詳細計測は、256件を超える候補にNOT SEEN／FLAGGED検索を使用する。

## 大量取得の比較

`--bulk-only`は20,000通×2KiB、2,000通×64KiB、20フォルダそれぞれ500通×2KiBを
応答待ち20msで各3反復する。旧30フォルダ条件と異なり、全フォルダに本文がある。
`--list-status`でサーバーのLIST-STATUS対応を有効化できる。
`run-bulk-guest.ps1 -Phase <未使用名> [-ListStatus]`で実行し、
`validate-bulk.py <回収先>`で45同期・9DBの各フォルダのUIDと件数を検証する。
`run-bulk-validation.ps1 -Phase <未使用名>`は全回帰・従来120同期に成功後、
LIST-STATUS非対応/対応の順に大量取得を測定する。GUI試験中にguestへ入力しないこと。

## フォルダ並列の比較

`run-folder-parallel-guest.ps1 -Phase <未使用名>`は全回帰と従来120同期に成功した後、
20フォルダ各500通・応答待ち20ms・LIST-STATUS対応で並列1/2/4/8を各3反復する。
各並列数で15同期・3DB。`validate-bulk.py`でフォルダ別のUID/原文/件数、
DB整合性、最大接続数が設定以内であることを検証する。
直接実行する場合は`--bulk-only --folders-only --list-status --parallel N`。
