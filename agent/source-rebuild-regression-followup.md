# 対応ソース再構築の修正後検証

2026-11-15。利用者依頼「検証で問題が出なくなるまで修正作業」に対する最終記録。修正後の提供物から標準入口で再構築し、通常／改変DLL構成ともSandboxで247成功・失敗0・スキップ0を確認した。公開用製品ZIPや最終legal承認とは区別する。

## 確認済みの修正

- `cb81db6`: Releaseで消える`_ASSERTE`を原因とする偽のクラッシュ待ちを是正。Debug assertionを維持し、両構成で実CRT abort・signal handler復元を検証。専用SandboxでRelease 9／Debug 10ケース、失敗・スキップ0。[診断記録](diagnostics-abort-regression.md)。
- `2afa313`: 固定tarと全ファイルtree SHA256でアーカイブ依存を検証し、別コピーへパッチ適用。Solid／WinFlexBisonを含む23アーカイブ、source commit引渡し、独立Qt構築に対応。[実装記録](source-archive-build.md)。
- `8a73094`: Windowsパスの大小文字違いによるSDK出力先ガードの抜けを修正。アーカイブ12ケース＋Qt出力先6ケースと、配布補助CTest 7対象に成功。

## 新規構築の入力

- `build/d04-source-review`: `2afa313`の23アーカイブ。独立展開・19 tree pin・7 KDEパッチ確認に成功（`build/d04-source-verify.log`）。
- `build/d04`: 上記提供物を使った標準`BuildWindows.cmake`経路。空のprefixからgettextホストツールも構築し、その後に配布用GNUを別profileへ構築。継続ログ`build/d04-build.log`。
- `build/d04/qt`: 提供物内の`BuildQtSourceReview.cmake`でQt Base/SVG構築完了。既存SDKとは独立。`build/d04-qt-build.log`、`build-profile.review.json`。
- Qtの厳格なパス検査は上流qsettings.cppの固定fallback `C:\temp\qt-common`／`C:\temp\qt-user`だけを検出。ソースの935／937行と既存許容方針を確認し、再構築QtCoreの全体SHA・2範囲のSHAを限定した技術レビュー入力で通過。作業者root／識別子は全バイトを検査し、他のQt DLL／プラグインは例外なし。ポリシーをDLL名で一括除外したり、バイナリを事後書換えしたりしていない（通常のwindeployqt prefix再配置は既存手順どおり）。`build/d04-qt-privacy-reviewed.log`と`build/d04/qt-path-policy.review.json`。製品の固定ポリシーやlegal承認を自動更新していない。
- `build/d05-source-review`: SDKガード修正を含む`8a73094`の23アーカイブ。全アーカイブのSHA256を照合してSmoothMailだけを`build/d05/SmoothMail`へ展開し、依存は標準入口が取り込む。
- `build/d05/deps`: 最新提供物からの新規product依存prefix。外部工具としてのみ、d04で新規構築済みのgettextホスト工具（`native/x64-windows`／`native/vcpkg`）を`host-tools`へコピー。d04の配布用GNU・KDE等のライブラリやbinary package cacheは流用しない。元の保存済み`build/deps`の工具／原本キャッシュは、検証を維持した読み取り元として使用するだけ。

## 実行と証跡の入口

- 最新標準再構築: `build/d05-build.cmake` → `build/d05-build.log`。
- 最新独立Qt再構築: `build/d05-qt-build.cmake` → `build/d05-qt-build.log`。
- 最新23アーカイブ検査: `build/d05-source-verify.log`。
- 全体構築後のテスト／改変DLL生成: `build/d04-validation-build.cmake`へ`VERIFY_ROOT=build/d05`、`SOURCE_REVIEW=build/d05-source-review`、`HOST_TOOLS_ROOT=build/d05/deps/host-tools`を渡す。提供物の既存関数を使い、初回のGit検証を置換する診断ラッパーは使わない。
- 展開用pack生成: `build/d04-stage-tests.cmake`へ`VERIFY_ROOT=build/d05`を渡す。product stageとテストEXE追加後のstageを分け、同一EXEの通常／改変DLL構成を比較する。

## 最終結果

- d04は空prefixからgettextホスト工具を含む標準構築に成功。d05も最新提供物`8a73094`から新規product依存・SmoothMail・独立Qt Base/SVG構築に成功。双方のホスト17対象、追加配布補助を含む24対象が成功。ホストはSafe Shell内でループバック接続できない3通信対象を明示除外し、資格情報や意図的クラッシュ等の個別スキップをSandboxで補完した。ホストをスキップ0とは扱わない。
- d05のネットワーク無効Sandboxで、通常／再構築・改変の各19テストEXEは247成功・失敗0・スキップ0。合成OAuth/通信、隔離された資格情報試験、CRT abort/SEH/terminate/Qt fatal、GUIワークフローを含む。外部の実メール・実アカウントは使用していない。
- 両構成でZIP展開後57ファイルのSHA256、GUI直接起動、配置先からロードされた20モジュールのSHA256を確認。通常構成のmarker不在という負対照、改変Qt SVG/KCodecsの追加export（45654）の実呼出し、API動作、GNU変換／翻訳catalog probeも成功。同一EXEで差替え可能だった。
- product stageは各34ファイル。`build/d05/source-binary-final.review.json`の68件で提供ソース／パッチ／構築出力／配置SHAを照合。QtCoreだけは通常のwindeployqt prefix再配置による差分であることを全バイト比較相当の期待SHAで確認し、他は構築出力と同一。
- 両product候補は全バイトの作業者root／識別子検査に成功。由来確認済みの既定値だけをexact SHA・範囲で限定した技術レビュー入力を使用した。review_only wrapperは製品ポリシーとして改名しても拒否。d04で範囲順が不正な入力を拒否した際はPowerShell生成側のoffsetソートだけを修正し、検査は緩和していない。
- Qt再構築profileはRelease／Schannel／内蔵SQLite／OpenSSL OFF。SDK側のOpenSSL・debug-and-release等との差は意図的なABI試験profileであり、SDKバイナリのビット完全再現は主張しない。独立したd04/d05間でもGui/Widgets/Network/Sql/Xml/Svgは一致したがCoreは不一致だった。
- 結果・診断sidecar・GUI画像は`build/d05-sandbox-output-final`へ回収済み。Sandbox `50440061-4d01-4e08-bce6-77b853e037ed`は終了成功を確認。既存Qt、通常起動先、以前の保存済み候補は変更していない。
- archive対応のlegalレビュー収集も成功。`build/d05-legal-baseline-review`について34ファイル／19コンポーネント／5 CRT DLLの証跡参照と、未承認資料の配布入力拒否（manifest改名を含む）を確認した。`build/d05-legal-review.log`。配布用`manifest.json`は生成していない。

## 主要証跡SHA256

| ファイル | SHA256 |
|---|---|
| `build/d05-source-review/source-manifest.review.json` | `606892c505c00c2dd3891a1c6f3e39b6e8343d823ece276a7d018cca3bbea53e` |
| `build/d05/source-binary-final.review.json` | `c5d8b1996c80118f3b75a243a414dc22cf5c2083b7b6bb54813766d5e9451a4e` |
| `build/d05/app/smoothmail.exe`（両構成共通） | `5e273a89a0f9c03597e1cde9ecd1357b2571c8c0bd350844e79b28a099f89505` |
| `build/d05-sandbox-output-final/result.json` | `a6ef9cc9698257764bf85c45560ae9850298bbb0a81dc5677dd21e91dd80e678` |
| `build/d05-sandbox-input-final/baseline.zip`（検証専用） | `c7081b04457e9f178ccb4df7d592b73b11336102467bf34173287297c5f98a2f` |
| `build/d05-sandbox-input-final/replacement.zip`（検証専用） | `55afb4b3680f4dcabe1444572188712f368faa0809e3d6410ab8ad5842ba9ae0` |

## 別途残る配布確定

今回発見した構築経路・入力不足・診断試験の問題は解消し、今回の技術再検証には失敗がない。最終通知・適用条件の確定、配布用legal manifest、最終製品バイナリとの対応確定、公開ページからのソース取得確認、legal付き製品ZIP全体の最終レビューはDIST-01/02に残す。利用者の「おおむね問題ない」との報告を最終承認に読み替えず、review_only制約と未承認ZIPの公開禁止を維持する。
