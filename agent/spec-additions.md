# 231eaa8：設定・検索・移動・Undo

## 実装

- SystemSettingsの公開値はQSettings。Google secretはCredentialStoreのsystem/google-client-secret。保存中は入力と閉じる操作を抑止。keychain成功後に公開値を保存し、失敗時は公開値・secretを復元する。永続ストレージ障害時の復元成功までは保証できない。
- GUI pageSizeは1～1000。MailPageSize=100は通信・DB投入の上限として維持。既知フォルダの選択はensureFolderLoadedでfolder_stateを調べるだけ。
- schema v5：folder_state、message_handles、pending_movesとdownloads.mime。effective_indexはpending_movesの宛先を重ねる一時view。本文は複製せずstorageFolderで物理ソースへ到達する。同じUID/UIDVALIDITYでもソース違いを区別する。
- stable handleはサーバMOVEの成功時に新UIDへ更新し、以前のフラグ操作・移動Undoも追従。pending_flags、downloadsと新しい移動intentも同時に移す。
- 接続先リセットやフォルダ削除・競合では既存handleをUID=0の墓標へ変更する。古いUndoが同じUIDVALIDITY/UIDを再利用した別サーバ・別メールへ結び直されることを防ぐ。
- MOVE送信直前にrevisionの存在を再検査。送信中の取消はreverse intentとして残す。attemptedは後続intentにも継承し、連続Undo/Redoで逆操作を消さない。
- COPYUIDが得られないとエラーで停止。次の同期はソース存在を検査し、消失済みなら盲目的に再送せず全体再取得。対応付け不能な古いUndoは失敗を返し、別メールに適用しない。
- フォルダ作成Undoはdelete-emptyを追記する。CREATE/DELETEのcapture済みバッチと競合しないよう、既存journalを消す方式は使用しない。redoもCREATE追記。
- メッセージDnDはセッショントークンと全行を検査後にsignalを送る。複数選択の移動はQUndoStack macroでまとめる。アカウント削除時は履歴をclearする。

## 制約

- クロスアカウント移動、永続Undo履歴は未実装。詳細はspec/provisional/settings-and-undo.md。
- サーバが返す操作失敗はstatus/errorへ表示し、再同期またはUndoで対処する。非同期に失敗した操作のUndo履歴を自動で巻き戻すことはしない。
- 実Microsoft/Google GUI認可、Release配布確認は引き続き対象外。

## 検証・成果物

- 最終オフラインWindows Sandbox：12 EXE、160 passed / 0 failed / 0 skipped。全EXEのTotalsとfailures.txtが空であることを確認。`build/spec-check/output-delivery/`。
- ホストCTest：非ネットワーク15対象成功。Python unittest discover：19件成功。SDKのインストール・変更は行っていない。
- 設定のguest keychain保存・再起動復元、手動のみのUndo、フォルダ作成/移動のUndo、既知フォルダの無通信復元、メールMOVEの対応/非対応/UID対応喪失、逆移動、フラグUID追従、同一UIDの複数ソース、接続先変更後の古いUndo拒否を検証。
- 開発用ランタイム42ファイルのSHA256、およびビルド/配備/シンボル保存用EXEの同一性を確認。`build/spec-check/symbols/`にPDB等を保存。
- 通常起動用`build/local-run`を更新済み。旧ランタイムは`build/local-run-before-spec-additions`。DBはv5へ進むため、旧EXEをそのまま新DBへ戻す用途には使わないこと。
- この作業で起動した3 Sandboxはすべて終了済み。実メール・実アカウントへの接続や実資格情報の出力はしていない。
