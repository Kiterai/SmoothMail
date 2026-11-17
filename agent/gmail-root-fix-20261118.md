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
