# DIAG-01: CRT診断のRelease/Debug回帰修正

2026-11-15。[対応ソース検証](dist02-source-rebuild-replacement.md)で見つかった`fatalExceptions(crt-assert)`終了待ち失敗の修正記録。

## 原因と修正

- `_ASSERTE`はReleaseでは消える。旧試験はクラッシュを発生させず`app.exec()`へ進んでおり、CRT assertionの処理不具合を再現した試験ではなかった。
- Debugでは既存の実assertion試験とCRT report hookを維持する。Releaseで有効なassertionを装ったり、成功扱いでスキップしたりしない。
- 両構成で実際の`std::abort()`を発生させるデータ行を追加。診断セッションがSIGABRTを捕捉し、固定理由`crt-abort`とモジュール名／相対offsetだけを記録して非対話で終了する。
- signal handler内でQt・ヒープ・mutex・例外文字列を使用しない。既存のfatal用Windows handle／固定stack記録を用いる。
- `finish()`で呼出し元のSIGABRT handlerを復元。繰返しstart/finishでも復元する試験を追加。

## 検証

同じソースをMSVC x64 Release／Debug、通常の/MD／/MDdで独立構築。新規・ネットワーク無効Windows Sandboxへ専用テストセットだけをコピーして実行した。

- Release: 9 passed / 0 failed / 0 skipped。
- Debug: 10 passed / 0 failed / 0 skipped（Debug CRT assertionを含む）。
- 実abort・Windows SEH・std::terminate・Qt fatal、機微文字列非混入、異常終了marker・stack offset、通常終了／強制停止、handler復元を確認。
- 実アカウント・資格情報・メールを使用しない。Debug CRTは開発者用の使い捨て検証に限り、製品へ含めない。
- 証跡: `build/d03-diag-build.log`、`build/d03-diag/output/{Release,Debug}.txt`、同`result.json`と設定別の診断sidecar。Sandboxは回収後に終了済み。

DIAG-01の局所的な再現・回帰確認は完了。提供ソースからの全体再構築とDLL差替えの再試験は別途実施する。通常起動先や保存済み成果物は差し替えていない。
