# 利用概要・保存領域の資料

対象: `2225073`、`spec/docs.md`。

- `docs/usage.md`にソフト名・概要と保存領域への導線を追加し、通知ディレクトリ名を現行の`licenses/`へ修正した。
- `src/main.cpp`、ApplicationControllerのQSettings、AccountRepositoryのSQLite schema、CredentialStoreのQtKeychain、添付保存と診断実装に基づき`docs/storage.md`を追加した。Windowsの保存先・秘密情報との分離・バックアップ・未同期変更と削除の注意を記述した。
- 配布READMEから相対リンクで開けるよう、配布補助は`docs/storage.md`もZIP直下へ未改変で収録する。runtimeへ誤配置しないこととSHA256一覧を既存package_guardsで確認する。
- 図・アイコン等の新規素材は使用していない。
- 配布補助をMSVC Releaseで再構築し、`package_guards`成功。独立展開、文書内容、配置先、全ファイルSHA256を検査した。
