# 並列更新・折り畳み余白

## 設計

- SystemSettings.parallelUpdatesを末尾に追加（既定4）。既存3項目の集成体初期化は互換。QSettingsのrefresh/parallelUpdatesへ保存し、既存の資格情報保存／設定ロールバックに含める。UI範囲は1～32。
- ControllerはMailServiceプールとアカウント別Active（Request、worker、serial）を保持。認証予約も枠に数え、同一アカウントは同時実行しない。待機対象をm_accountsの現在順で走査して開始する。全更新への昇格・folderOnlyの区別を維持。
- 上限縮小はdrain、拡大はprocessNextで補充。削除時は当該アカウントだけcancelし、通信開始済みの枠はsynchronizedまで保持。認証開始前の予約解放後の古いcallbackはserialで拒否する。
- SQLiteは既存の単一workerを維持し、書込みを直列化する。ネットワーク／MIME処理は複数workerで並列。TEMP sync_messages/sync_indexにaccount,uidの一意索引を設け、begin/prune/reuse/count/commit/abortをアカウントで分離。同期のfolder/UIDVALIDITY/operationもアカウント別に保持する。実行中に別アカウントのabortでstageを破棄しない。
- Repositoryの省略accountIdは従来の単一セッション試験向け。MailServiceの全staging呼出しは明示的にaccount.idを指定する。永続DB形式はv5のまま。
- MessageViewerは固定基本情報と、ヘッダ＋本文のSplitterに分離。ヘッダを隠すとhandleも隠れ、本文が余白を使う。トグル文字は▶/▼。既存フォント・hover背景・プレインテキスト処理は維持する。新規画像アセットなし。

## 回帰

- Repository：同じUIDを使う2アカウントでbegin/stage/commitを交互実行し、誤ったaccountIdのbatchを拒否する。別アカウントのrecent-window削除／abort後にもstageを保存できることを確認。
- GUI：4アカウントの合成サーバでheader応答を停止。先頭2枠だけ開始すること、上限2→1で既存処理を中断しないこと、先頭失敗が他に波及しないこと、停止中のアカウント削除で次へ進むこと、1→2で待機を補充すること、並行保存後の本文識別情報を確認。
- 設定：既定4、保存失敗時の編集維持、再起動後の並列数復元。Viewer：非展開時の余白とhandle非表示、展開後handle表示、再折り畳み／リサイズで本文に高さを戻すことを確認。

## 最終結果

- `build/parallel-check/output/` の全12 EXE Totalsを独立集計：196 passed / 0 failed / 0 skipped。failures.txt空、complete.txtあり。ホストCTest15対象とPython31件も成功。
- 最初のホストViewer試験では、ヘッダ展開直後にQtの遅延レイアウトを待たずhandleの可視性を検査して失敗した。QTRYでレイアウト完了を待つよう修正し、ホスト全対象とnative全EXEで成功した。
- Sandbox `08135b11-3ed4-4b48-a8d1-f94dc41696d6` は終了成功。実DB・メール・資格情報へアクセスしていない。`.agent-config/`は利用者管理の未追跡状態を維持。
- `runtime/`全42ファイルのinventory、全テストEXEとbuild/dev、配備EXEのSHA256一致を検査。起動中のSmoothMailがないことを確認し、旧local-runを`build/local-run-before-parallel`へ移して検証済みruntimeを配備した。PDBは`symbols/`、GUI画像は`output/screenshots/`に保持。
