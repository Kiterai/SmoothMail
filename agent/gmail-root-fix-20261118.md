# Gmail仮想親フォルダへのSTATUS拒否（2026-11-18）

## 調査

- 利用者の画像: `Status failed ... NO Invalid folder ... NONEXISTENT Gmail`。
- 直前の高速化で認証後LIST-STATUSを利用し、通常LISTのフォルダ一覧を拡張LISTで置換する。
- Gmailは通常LISTで`[Gmail]`に`\Noselect \HasChildren`を返すが、
  拡張LISTでは`\NonExistent \HasChildren`を返すことがある。
- 実装は`\Noselect`のみを見ていたため、拡張一覧から`[Gmail]`を選択可能と誤認し、
  STATUSを送ってNO [NONEXISTENT]に至る経路がある。
  画像だけで利用者の実応答を断定せず、この公開済み応答形を模擬サーバーで検証する。
- RFC 5258 §3/§3.4は`\NonExistent`が`\NoSelect`を含意し、
  クライアントがその含意を扱うことを要求する。

参照:
- https://www.rfc-editor.org/rfc/rfc5258.html
- https://developers.google.com/gmail/imap/imap-extensions
- https://github.com/jstedfast/MailKit/issues/28

## 修正

- coreの`isSelectableFolder`でNoSelect/NonExistentを大小文字非依存で判定する。
  同期、下書き探索、UI、キャッシュ初期選択、移動先検証で共通化する。
- 同名LIST行の統合で実体の選択可能な行が確認できた場合は、
  placeholderのNoSelect/NonExistent両方を除去する。両方placeholderなら取得しない。
- `[Gmail]`という文字列による除外や、STATUSエラー全般の握り潰しは行わない。
  階層ノードは表示に残し、子の実メールボックスは通常通り取得する。
- メールDBの削除/リセット、認証方式変更、並列化の無効化は行わない。

## 試験準備

- 通常LISTではNoSelect、拡張LISTでは混在大小文字NonExistentを返し、
  親へのSTATUS/SELECT/EXAMINEはNO [NONEXISTENT]を返すfixtureを追加。
- Gmail形式・一般名の階層ノード、並列取得、同名行の順序違い、キャッシュ初期選択、
  UIで親は選択不可/子は選択可を検査する。
- ホストでの最初の限定試験はloopback接続が時間内に完了せず、
  NoSelectのみの既存経路も含む4ケースすべてtimeoutとなった。
  `build/gmail-root-before.txt`を保持し、原因再現の根拠には数えない。
  原因再現と最終通信回帰は外部ネットワーク無効guestで行う。

## 修正前の再現確認

- `build/gmail-fix/share/output/final-baseline/before.txt`。
  同じfixtureで、通常LISTは成功、拡張LISTの3ケースはSTATUSで拒否された。
  修正前EXEの結果は3 passed / 3 failed / 0 skipped（init/cleanupを含む）。
- Gmailケースの実エラーは
  `Status failed, server replied: A000007 NO Invalid folder: ( Failure ) [ NONEXISTENT Gmail ]`。
  画像のエラー文字列・コマンド段階と一致した。一般名Containerでも同じ経路が再現し、
  Gmailの名前固有の処理ではなく、属性の含意を扱わなかった不具合と確認できた。
- 起動中のSmoothmailプロセスを検出した。強制終了、実メール/認証情報/ホストDBへの操作は行わない。
  検証後の修正版は別Release配置に生成し、通常配置へは終了確認後に反映する。

## 最終検証

- 修正コミット`6c710d1700f11716c5b4120a361470a0a017dc6a`。
  `build/gmail-fix/share/output/final/tests`の全17EXEで
  **303 passed / 0 failed / 0 skipped**。
- transport61（Gmail形式4ケースと重複NonExistent3ケースを含む）、
  repository32、mainwindow52、native GUI統合10、その他148。
  rootが表示に残ること、子の本文を取得できること、rootへのSTATUS/SELECT/EXAMINEを
  送らないこと、キャッシュからrootを初期選択しないことを確認した。
- 従来120同期も全成功。24DBのquick_check、UID/件数、ヘッダ/本文取得件数を照合成功。
  並列/TLS/取消/途中失敗/保留フラグ/3秒周期表示の既存回帰も成功。
- ホスト非通信CTest14対象成功、Python52件成功（既存条件の8 skip）。
  旧版での期待した3失敗やホスト通信timeoutを、修正版の成功件数に含めていない。
- 通信/GUI試験Sandbox `fc33d9e0-4948-4e34-8d1b-c16151af9fe1`は回収後に終了成功。

## 修正版Releaseと配置状態

- アプリ起動中の通常配置を無理に置き換えず、標準Release workflowを
  `build/gmail-fix/release`へ実行。終了コード0、ZIP/通知/対応ソース/検証済み展開物を生成した。
  `SMOOTHMAIL_DISTRIBUTION=ON`。構築と対応ソースmanifestのcommitは上記6c710d1と一致。
- 起動先: **`build/gmail-fix/release/run/launch.cmd`**。
  manifestの360ファイルのSHA256と、app EXE/配置EXEの一致を確認した。
  本体SHA256は
  `2db5862ccaeb37d98a777e9006e30a2dd42990fa8361347140176778f7e6549e`。
- 外部通信無効の新規Sandbox `30c32a46-4a60-40e4-be6a-b01ca3f4764a`で
  このRelease一式をguestへコピーし、空状態のネイティブ画面とqsqliteロードを確認。
  ロードモジュール104件はguest配置先/Windows配下のみ、本体hashも一致した。
  `build/gmail-fix/share/output/release-{startup,modules,exe-sha256}.json`へ回収後に終了成功。
- 起動中の通常アプリは停止していない。`build/rel/run`の本体hashは反映前の
  `4bd210ef5547d703375e590e3013aa76a9c3bb43516bbd62a58051ba5dc54a92`で不変。
  通常配置への反映は利用者による終了確認待ち。
- 実Gmail認証・実メールへの接続は行っていない。実サービスでの再確認は利用者の試用が必要。
  DBや設定のリセット、SDK/ACL/Git global設定の変更も行っていない。

## 通常配置への反映（利用者の終了確認後）

- 利用者の「終了しました。」を受け、Smoothmailプロセスが検出されないこと、
  反映前の通常EXEが上記4bd210ef…54a92であることを確認した。
- 検証済みReleaseを同じ親の一時配置へコピーし、manifest360ファイルと全361ファイルの
  inventoryを照合してから通常runを置換。配置後も同じ照合に成功した。
- 現在の起動先 **`build/rel/run/launch.cmd`はGmail修正版**。
  本体hashは上記2db5862c…6549eで、検証済み別Releaseと同一。
- 旧版一式は`build/rel/run-previous-gmail-fix-20261118-073641-cff00bb4`へ退避。
  配置証跡は`build/gmail-fix/normal-deployment.json`。
  通常runのみの反映であり、生成ZIP/対応ソースは`build/gmail-fix/release/artifacts`に保持する。
- ホスト上のアプリ起動、実Gmailへの接続、メールDB/設定/資格情報の変更、
  アプリ強制終了、ACL変更は行っていない。
