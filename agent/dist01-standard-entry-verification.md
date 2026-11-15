# DIST-01: 最終入力での標準配布入口の通し検証

2026-11-16。利用者から、標準単一コマンド入口の実行、問題があれば修正、成功した場合の完了マークを依頼された。DIST-02の利用者確認済み状態は維持する。公開・push・実メール利用・通常起動先の更新は行わない。

## 入力の現状

- 開始時のHEADは`44366d2`。追跡ファイルはclean、既存の未追跡`.agent-config/`は変更していない。
- 前回生成先の`build/`自体が存在せず、監査済み最終入力`build/nov16-release2/licenses`及び最終ソースZIPも利用できなかった。プロジェクトと近傍の開発ディレクトリ、Downloads/Documents直下で対象名のZIPは見つからなかった。削除・移動の理由は推測しない。
- Qt `C:/Qt/6.12.0/msvc2022_64`、CMake 3.30.0、VS 2022 x64開発者環境（MSVC 19.44.35229）は利用可能。実`.env`の存在のみを確認し、本文は読み出し・変更・ログ出力していない。
- 監査済みライセンス一式は、テンプレート又は`.env`だけでは第三者原文を含めて復元できない。架空のmanifestや原文省略で代用せず、確認済み入力の復元を待つ。再レビューを利用者へ要求するものではない。

## 検出した不具合と修正

`cmake/PackageWindows.cmake`は`LEGAL_DIR`を文字列として要求するだけで、最終入力が欠落していても`DependencyBuild.cmake`によるNinja取得・構築へ進んでいた。入口に次の事前検査を追加した。

- `LEGAL_DIR`は既存の通常ディレクトリであり、シンボリックリンクではない。
- `manifest.json`は存在する通常ファイルであり、リンク・ディレクトリ・空ファイルではない。
- 不備時はCRT探索・依存取得より前に停止する。

manifestの内容・全参照・UTF-8・review-only拒否は、従来どおり構築後のpackage helperで検査する。事前検査の追加はこの検査の代替や緩和ではない。

入口ガードは3ケースから8ケースへ拡張した。既存ZIP保持、ZIP拡張子、CRT入力に加え、ライセンスディレクトリ欠落・manifest欠落・manifestがディレクトリ・manifest空・LEGAL_DIRがファイルを、それぞれ想定した理由で拒否することを確認した。全ケースで依存root及び新しいZIPが生成されないことも検査し、8ケースすべて成功した。

配布補助ツールをMSVC／NMakeのRelease構成で新規構築し、CTest10対象すべて成功（失敗0、`build/dist01-tools-retry.log`）。初回の一行呼出しはcmdの`%PATH%`先行展開によってvcvars追加分を失いnmake未検出となったため、開発環境の設定とPATH追加を別行の`build/dist01-tools.cmd`へ分離して是正した。製品コードや検査条件を回避したものではない。

## 標準入口の実行結果

MSVC x64開発者環境を適用し、次の標準入口を実行した。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DLEGAL_DIR=build/nov16-release2/licenses -DDEPENDENCY_ROOT=build/dist01-verification/deps -DOUTPUT=build/dist01-verification/SmoothMail-windows-x64.zip -P cmake/PackageWindows.cmake
```

終了コード1。欠落している最終入力を事前検査で拒否した。ログは`build/dist01-package-entry.log`。依存構築・アプリ構築・ZIP生成には到達しておらず、通し検証成功とは扱わない。新しいDIST-01検証用の成果物ZIPは生成していない。

## 残件

**DIST-01は未完了（監査済み最終入力の欠落により保留）。** 確認済みの`SmoothMail-0.1.0-sources-20261116-r2.zip`又は展開済みの`licenses/`・`archives/`の所在が必要である。復元後、対応する製品ソース識別子とライセンス／ソース案内の整合を保って標準入口を実行し、全構築・最終ZIP・SHA256／同梱内容検査まで成功した場合に完了とする。入力不備の拒否試験だけで完了マークを付けない。
