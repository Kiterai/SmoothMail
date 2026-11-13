# 返信元ジャンプ・明確なSplitter

- VisibleSplitterは通常のQSplitterHandleのドラッグ処理を維持し、paintEventでWindow背景と対照的な白／黒の1px線を7pxハンドル内に描く。利用者の細線指定により2pxから変更。Qtのスタイルが境界を背景へ溶け込ませる場合も線を維持する。3か所で共通利用し、ヘッダ非展開時はハンドルを隠したまま。
- ComposeDialogのcomposeReplySourceはPlainText QLabel。件名はHTML解釈しない。返信元の有無は件名の空／非空ではなくControllerから明示する。ホバー背景とキーボード操作を用意し、マウスはラベル内の左ボタン押下・解放のみを受け付ける。
- 返信作成時に本文を除いたMailRecordでUUIDハンドルを登録。Repositoryの既存message_handlesによるMOVE後UID・フォルダ改名・接続先変更の無効化に追従する。クリック時はeffective_indexと結合して物理ソース＋pending move＋pending folder renameを解決する。lookupは未知のハンドルを古いUIDから作り直さない。
- messageLocatedの遅延応答は、現在のm_composeとm_replyHandleが一致するときだけ反映。失敗時はcompose内にエラー表示。成功時はメインを表示しcomposeをhide（破棄しない）。既存composeを開く経路にshowを追加した。
- MailQuery.revealStorageFolderとMainWindowのreveal識別情報を追加。pending moveで同じUIDVALIDITY/UIDの複数物理ソースが同じ表示先に存在しても区別する。ページ順位とページ取得の並び順はstorage_folderのタイブレークを共通化する。永続DB形式はv5のまま。
- 回帰：明暗×縦横の線描画／ドラッグ、全Splitterの共通化、ラベルのリテラル表示・マウス／キー・空件名、新規メールの非活性、入力保持、物理ソース衝突・MOVE後UID・改名・UIDVALIDITY変更・削除・ハンドル解除を検査。native Controllerではフィルタで元メールを隠した後にクリックし、元の本文表示、フィルタ解除、同じ作成画面／本文入力の復元を確認する。

## 最終検証・配備

細線指定への追従：1pxの線に加え、隣接画素が背景色であることを明暗×縦横で検査。ビルドとホストCTest15対象が成功。初回リンクでtst_messageviewerのincremental COMDATエラーLNK1163が出たため、生成済み.ilkを`.ilk-before-thin-splitter`へ退避して再リンクし成功した。新しいSandboxは起動していない。新runtimeは`build/thin-splitter-check/runtime/`、SmoothMail起動中のため通常local-runは未変更（spec/inprogressのUI-01）。以下の202件と配備記録は2px版の実施履歴。

その後、利用者の終了報告を受け、Get-ProcessでSmoothMailがないことを確認して1px版をlocal-runへ配備済み。ステージ全42ファイルのSHA256とbuild/dev EXEを照合し、旧版を`build/local-run-before-thin-splitter`へ保持。配備後の全ファイルも照合した。UI-01は完了とし、記録を`docs/dev/thin-splitter-deployment-20261114.md`へ移した。アプリの起動や実アカウント接続はしていない。

- `build/reply-source-check/output/` の12 EXE全Totalsを集計：202 passed / 0 failed / 0 skipped、failures.txt空、complete.txtあり。ホストCTest15対象、Python31件も成功。
- GUIの03-populated-main-headers.pngと05-reply-dialog.pngで明確な境界線・ラベル・ホバー背景・維持された作成本文を目視確認。fixtureのみで、実サービス再試験ではない。
- Sandbox `5839c19a-73b4-4779-82a1-fba409351519` は終了成功。runtime全42ファイルのinventory、全テストEXEとbuild/dev、配備EXEのSHA256一致を確認。SmoothMailが起動していないことを確認して旧local-runを`build/local-run-before-reply-source`へ移し、検証済み版を配備した。PDBはsymbols/に保持。
- `.agent-config/`と利用者のDB・メール・資格情報は未変更。既存のinprogress項目の範囲や、AUTH-01／CHECK-01の利用者による完了記録は変更しない。
