# 2026-11-13: 読込途中のCRT例外の調査

## 再発ログ（UTC 02:01:59開始）の追加調査

再発ログのEXE SHA-256は`456d31111bc4c74d3ad867c030ca85815fad5316c61e373c5a0336dd8a005172`で、前回の修正版EXEと一致しました。最後のイベントはUTC `02:05:12.799 sync-header-batch 5901`、例外分類は`crt-assert-ctype-domain`です。呼出し履歴には`KF6Codecs.dll+0x1ce3a`、`KF6Mime.dll+0x6d540`、KIMAPのヘッダ解析経路が記録されています。

同梱KCodecsの逆アセンブルでは`+0x1ce34`が`isspace`の呼出し、`+0x1ce3a`がその直後で、引数は`movsx`で符号拡張されていました。上流`src/kcodecs.cpp:372`の`decodeRFC2047String`が`isspace(*scursor)`を実行する箇所と一致します。KMimeだけを修正しても、依存するKCodecsに同種の不具合が残っていました。前回の回帰入力はDate等に偏り、非ASCII Subjectを検査していなかったため見逃しました。

前回ランタイムと拡張した合成テストを使い、非ASCII SubjectとRFC 2047 encoded-word混在Subjectの2ケースが異常終了しました（9 passed / 2 failed、init/cleanup含む）。固定分類とKCodecsの相対停止位置は今回の実ログと一致しました。実際の入力値やメールは調べていませんが、今回のアサート発生箇所は特定できています。記録は`build/codecs-check/baseline/`です。

### 追加対処と点検範囲

- `patches/kcodecs/6.31.0-safe-ctype.patch`で該当引数をunsigned charへ変換。独立したソースコピーから正規MSVC Debugで構築し、Qt SDK・元のソースは変更しません。
- KCodecsにもコミット/パッチ/DLL/Qt Core/構成receiptを導入し、アプリconfigure時に検証します。元のライセンス・変更告知を保持します。
- 既存5種類にSubject、From/To表示名、encoded-word混在Subjectの4種類を追加し、128値ずつ合計1152入力を子プロセスで検査します。通常の日本語、encoded-word間の空白処理、原文不変も検査します。
- 10034件の同期テストの5901件目に、不正Dateに加えて非ASCII Subjectを挿入しました。
- KCodecs/KMime/KIMAP/KSMTPの`src`にあるC文字分類/変換呼出しを点検。KCodecsの該当箇所以外は、KMimeの既存修正箇所とKIMAPの`ImapStreamParser::readNumber`に`isdigit`がありました。後者は同ライブラリの現行ソースに呼出し元がなく、今回の解析経路にもないため、この修正では変更しません。これは依存全体の未定義動作がないという保証ではありません。KSMTPには該当呼出しが見つかりませんでした。

以下は最初のログの調査履歴です。前回の128件成功だけでは今回の入力経路を検証できていなかったため、追加検証と区別してください。

### KCodecs修正後の最終結果

Windows Sandboxで11 EXE、**133 passed / 0 failed / 0 skipped**（init/cleanup含む）。mimeregressionが12件、その他は前回と同じ121件です。1152合成入力、日本語とencoded-word混在の復号結果、5901件目に非ASCII Subjectを含む全10034件同期、途中切断/再同期、GUIページ表示を検証しました。ホストはネットワーク系3対象を除くCTest15対象、Python配置3件・receipt6件が成功。記録は`build/codecs-check/output/`です。

配置42ファイルのハッシュ、EXE/KCodecs/KMime DLLとビルド・保存シンボルとの一致、登録JSON・テストEXEの非混入、旧版からのKCodecs DLL更新を確認しました。`build/local-run`を更新済みで、通常の`build/start-smoothmail.cmd`から起動できます。直前のランタイムは`build/local-run-before-codecsfix`、配置ログは`build/codecs-check/stage.log`、EXE/DLL/PDBは`build/codecs-check/symbols/`です。EXE自体は同じでも、KCodecs DLLが更新されています。今回起動したSandboxは両方とも終了済みです。

元の実メールは調べていないため、利用者環境で最後まで読み込めることの最終確認は更新後の再試行で行います。

## 添付ログから確認できた事実

- 例外コードは`0x80000003`、モジュールは`ucrtbased.dll`、相対位置は`0x97627`。同梱CRTのこの位置は`_VCrtDbgReportW`内部の`int 3`で、アサート報告中の停止と整合します。アクセス違反やメモリ不足そのものを示すコードではありません。
- UTC `01:08:40.428`の`sync-header-batch 5901`が最後で、そのバッチの`sync-fetch-batch`はありません。5900件分の処理後、次の100件のヘッダ取得/依存ライブラリによる解析中に止まっています。
- 正常終了行がなく、activeマーカーも残っています。
- 旧ログにはアサート内容も呼出し履歴もありません。この情報だけで、特定の実メールや入力ヘッダ、唯一の根本原因を確定することはできません。

利用者のDB、メール原文、OS資格情報は調査のために読み取りません。実サーバーへの接続も行わず、添付された診断情報だけを参照します。

## 合成入力で再現した不具合

KMime 6.31.0の`headerparsing.cpp`等は、`isdigit`/`isspace`/`toupper`へ符号付き`char`を直接渡しています。C文字判定関数に許される引数はEOFまたはunsigned charの値であり、0x80..0xfeの負の値は引数範囲外です。MSVC Debug CRTはこれをアサートにします。非Debugでも未定義動作なので、Release化やアサート無効化は修正ではありません。

変更前のDLLと小さい合成入力を使った子プロセスで、非ASCIIバイトのあるDate/Control/Newsgroupsの3ケースがそれぞれ異常終了しました。先に導入した安全なCRTフックで`crt-assert-ctype-domain`とKMimeを含むコードフレームを回収しています。記録は`build/crash-check/baseline/`です。実メールは一切使っていません。

この不具合は添付ログの状況に合う**再現済みの原因候補**ですが、利用者の実入力が同じだったと断定はしません。特に旧ログのCRT内の位置だけでは、別のアサートや破損状態の可能性まで除外できません。

## 対処

- `patches/kmime/6.31.0-safe-ctype.patch`で、ヘッダ・uuencode/yEnc検出・Windowsのstrcasestr補助処理の該当バイトをunsigned charへ変換します。解析規則や原文を勝手に置換せず、不正なDateは通常の解析失敗/無効日時として扱います。
- 固定コミットから独立した修正ソースを作成してMSVC Debugで再構築。Qt SDKは変更しません。上流ライセンス、差分、変更告知と、コミット/パッチ/DLL/Qt Core/構成のreceiptを保存します。アプリconfigure時にもKMimeのreceiptを検証します。
- 診断にCRTアサートとQt Fatalの固定分類、スレッドID、モジュール名/相対コード位置の呼出し履歴、EXE SHA-256を追加しました。アサートの継続や無視はせず、記録後に異常終了します。CRT/QtのDebugダイアログ待ちを避けます。
- 任意のCRT/Qt診断本文、例外文字列、実メモリ、メール本文・ヘッダ・認証情報は保存しません。標準のQt出力先へ任意の解析診断を転送せず、明示的な既存ハンドラー（Qt Test等）がある場合だけ維持します。通常の接続/保存エラーは従来どおりUIで報告します。

## 回帰検証

- 子プロセスでDate/Control/Newsgroups、uuencode/yEnc検出の5種類について、0x80..0xffの128値ずつ（合計640入力）を検査。
- 1万34件の分割同期テストの5901件目に非ASCIIの不正Dateを挿入。ヘッダ取得、本文解析、全件公開、再同期が成功することを検査。
- Windows例外、std::terminate、CRT assert、Qt Fatalについて、秘密を含む固定テスト文字列が診断へ漏れず、コード位置が保存され、子プロセスが止まることを確認。
- 通常のMIME、日本語、添付、ページ表示、アカウント管理、OAuth合成サーバー、TLS拒否などの回帰テストも使い捨てSandboxで実行します。修正後の記録は`build/crash-check/fixed-output/`です。

元の事象の解消確認には、更新したランタイムで同じ読込を再試行する必要があります。再発時は新しい診断の固定分類と呼出し履歴から調査を継続します。メール原文を共有する必要はありません。強制停止、fail-fast、重大なスタック/ヒープ破損、ディスク障害などで記録できない場合がある点は変わりません。

### 最終結果と手元ランタイム

正規MSVC Debugの修正DLLで、Sandboxの11 EXEは**128 passed / 0 failed / 0 skipped**（init/cleanup含む）。ホストCTestはネットワーク系3対象を除く15対象、Pythonは配置3件とreceipt6件が成功しました。元の異常終了の再現試験とは別に、修正後の結果を`build/crash-check/fixed-output/`へ保存しています。今回起動したSandboxはすべて終了済みです。

最終配置の42ファイルのSHA-256と、EXE/DLLの現在ビルド・保存シンボルとの一致を検証しました。通常の`build/start-smoothmail.cmd`が使う`build/local-run`を修正版へ更新済みです。直前のランタイムは`build/local-run-before-ctypefix`へ保存し、対応するEXE/KMime DLL/PDBは`build/crash-check/symbols/`に保持しています。配置ログは`build/local-run-crashfix-final-stage.log`です。登録JSON・秘密情報・テストEXEは配置していません。
