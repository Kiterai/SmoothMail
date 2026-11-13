# LIST同名項目の統合

## 調査根拠

提供ログは新診断版のもの。op=1/2は同期成功、op=3でraw LIST16項目（1 batch）、保存時duplicate_count=1/first_duplicate_index=16/duplicate_of_index=2、empty_count=0/negative_unread_count=0。sync-cache-rollback→sync-cache-failed→sync-failed、最後にsession-clean-exit。本文取得後に初回commitSyncで失敗している。実フォルダ名は不明であり、合成試験のINBOXは仮名。

原因を「受信側で一意化していない一覧を、保存側の重複禁止条件へ渡すアプリの互換性不足」と特定。応答原文・実名を持たないため、wire上の重複かKIMAPのデコード/convertInboxName後の同名化かは未確定。実サーバやKIMAP自体のバグと断言しない。提供ログをリポジトリへコピーしない。

## 実装

- MailService::synchronize内のlistFoldersにQHash<QString, ListedFolder>を置き、初出のunique位置とraw応答番号を保持。callbackを跨いで保持し、LISTをやり直すたびに初期化する。
- 同名・同separatorならフラグをケース非依存で和集合。いずれかがselectableならNoSelectを除去、両方NoSelectなら維持。HasChildrenがあればHasNoChildrenとNoinferiorsを除去。その他の属性は保持。
- QString自体のcasefold/NFC等は追加しない。KIMAPが既に正規化したINBOXだけを同じ名前として扱う。Archive/archiveは別。
- 空名は統合せず従来の保存検証で拒否する。raw応答数10000の制限は重複を除く前に適用する。同名でもseparatorが違えば明示的に失敗し、数値のseparator/項目番号だけを診断へ出す。
- raw総数、統合数、最初の重複と元項目のraw番号、unique総数を固定event＋数値で記録。storageの同名拒否は残す。

## 合成回帰

ConnectionTesterに9データ行：同一バッチ、raw16=raw2を300ms遅延して複数バッチ、placeholder→selectable、逆順、両方NoSelect、separator競合、大小文字で異なる一般フォルダ、raw10001件、inbox/INBOX正規化。

STATUSが各箱1回、SELECT/EXAMINEは初回＋最終整合性確認の1組、INBOX本文がDBへ保存されること、属性保持、raw件数上限を迂回しないこと、競合時にSTATUS/SELECT/本文取得を開始しないこと、エラー後の正常再取得、重複があるまま保留CREATE→再LISTが成功することを検査。診断へaccount ID/email/INBOX/password/host原文が出ないことも検査。

初回native試験ではSELECTの期待値を1と誤認していたため、最終整合性確認を含む2へ修正（実装の検査は削除しない）。全NoSelectの行では既存のfolder_state.folder NOT NULL違反も検出した。beginSyncのsyncFolderをnull QStringから空QStringへ正規化し、Repositoryにも独立回帰を追加。提供ログの原因とこの別件を混同しない。

## 最終検証・成果物

- オフラインWindows Sandbox12 EXE：181 passed / 0 failed / 0 skipped。全Totalsとfailures.txtの空を確認。最終ログは`build/list-dedup-check/output-final/`。初回の失敗結果`output/`とは区別。
- ホスト非ネットワークCTest15対象、Python31件が成功。今回起動したSandbox2台は両方終了済み。
- `runtime-final/`の42ファイルのSHA256とbuild/dev・symbols・配備用EXE同一性を確認し、`build/local-run`を更新。旧版は`build/local-run-before-list-dedup`。PDB等は`build/list-dedup-check/symbols/`に保持。
- 実DB・アカウント・メール・認証情報にはアクセスせず、提供された診断ログの読取りと合成試験だけを行った。実アカウントでの修正確認は利用者の再取得待ち。SDKや依存の変更はしていない。
