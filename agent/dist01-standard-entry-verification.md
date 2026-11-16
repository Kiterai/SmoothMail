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

## 入力復元前の保留記録

この時点ではDIST-01は監査済み最終入力の欠落により保留とした。確認済みの`SmoothMail-0.1.0-sources-20261116-r2.zip`又は展開済みの`licenses/`・`archives/`の所在を確認し、対応する製品ソース識別子とライセンス／ソース案内の整合を保って標準入口を実行する。全構築・最終ZIP・SHA256／同梱内容検査まで成功した場合に完了とし、入力不備の拒否試験だけで完了マークを付けない。入力は後続の再開記録の通り復元済み。

## 退避入力の復元後の再開

利用者からソースZIPを`build/`へ戻した旨の連絡を受領した。ZIP全体のSHA256は`dcc3a335540874d72663733cb96b1343eb05c45d68526849d8a71e76da0f9760`、149755868バイトで、先行記録とは異なった。その後、利用者から「以前のものを展開して再圧縮した」との説明を受領した。

ZIPの安全な相対パス・重複／リンクの拒否を検査し、内部348ファイル（ディレクトリ込み462エントリ）が同梱SHA256一覧とすべて一致することを確認した。加えて、本体tarは先行記録のSHA256、依存19 tarはリポジトリの固定SHA256／commit、Cyrus SASL・GNU原本3件は既知のSHA256／SHA512と照合し、計23アーカイブすべて成功した。ZIP全体の差を無視して未検証の入力を採用したものではない。検証スクリプトとログは`build/dist01-restore.ps1`・`build/dist01-restore.log`。

入力は`build/dist01-restored-input/SmoothMail-source`へ独立展開し、package helperの`--check-legal`でも配布用manifestと全参照テキストの検査が成功した。本体tarは`build/dist01-exported-source/SmoothMail`へ展開した。確認済みソース`aba52dac6b049d8cd2b941c54301d82fcb9eadce`と通知内の対応ソースを一致させるため、この未改変の提供済みソースに含まれる標準`PackageWindows.cmake`を実行する。現在の記録用HEADを製品ソース識別子へ流用しない。

```bat
cmake -DQt6_ROOT=C:/Qt/6.12.0/msvc2022_64 -DLEGAL_DIR=build/dist01-restored-input/SmoothMail-source/licenses -DSMOOTHMAIL_SOURCE_ARCHIVES=build/dist01-restored-input/SmoothMail-source/archives -DSMOOTHMAIL_SOURCE_COMMIT=aba52dac6b049d8cd2b941c54301d82fcb9eadce -DDEPENDENCY_ROOT=build/dist01-full/deps -DOUTPUT=build/dist01-full/SmoothMail-windows-x64.zip -P build/dist01-exported-source/SmoothMail/cmake/PackageWindows.cmake
```

MSVC開発者環境を設定する`build/dist01-run-final.cmd`から上記を実行。新規Ninja内部構築、package helper構築、最終legal検査まで成功した。Nativeステージでvcpkg実行ファイルを取得する際、サンドボックス内のHTTPS取得がCMake status 35で停止した（`build/dist01-full-run.log`）。既知のWindows TLS環境制約であり、証明書検証やハッシュ検査は無効化しない。

同じコマンドの通常環境実行についてSafe Shellの許可を受領し、実行を開始した。vcpkg及びCMake／7zip等の検証付き取得に進み、サンドボックス内TLSエラーは通常環境では解消した。依存・アプリの全構築とZIP生成・最終検査は実行中であり、成功したことにはしない。入力欠落による保留は解消済み。

- 実行コマンド: `build\dist01-run-final.cmd`（通常環境、MSVC x64設定後に上記標準入口を実行）。
- Safe Shell execution ID: `04bf48a74b994284bf79a3e05a7c8b0c`。
- 完全出力のjournal log ID: `06aa8fc2bb854e71b8ff350b7e68f11a`。stdout／stderrを別々に保存・参照する。
- 後続検査: `build/dist01-verify-output.ps1`で全ZIPファイル／inventory、確認済み通知321参照、EXEのソース識別子、同梱文書と実コンパイル入力の一致を独立検証する。出力先は`build/dist01-full/verification.json`。終了コード0及び後続検査成功までDIST-01を完了扱いにしない。

## 全依存構築後に検出したCRT検証器のパス長問題

通常環境の上記実行は全依存のRelease構築とアプリのリンクまで成功したが、配置処理でCRT検証器を新規構築するときに終了コード1となった。展開済み製品ソースのroot長79文字に、検証器キャッシュ名内のSHA256全64文字とCMakeのTryCompile階層が加わり、コンパイラ試験のobjectディレクトリが238文字となって、MSVCのパス長制限を超えた。CMakeの警告とC1083／Invalid argumentを確認した。署名や原本照合の失敗ではなく、検証器を構築できなかったものである。

現リポジトリの`WindowsCrt.cmake`はキャッシュ名を`crt-v2-<hash先頭16文字>`へ短縮した。フル64文字の識別子を`source-key.txt`へ保存し、短縮名が衝突した場合は再利用を拒否する。CRT署名・x64・版・原本一致の検査は省略しない。深い展開先での実MSVC構築と識別子衝突拒否の回帰を追加し、配布補助CTestは11対象すべて成功した（`build/dist01-crt-path-regression.log`）。

確認済みの製品ソースやZIPを後から改変しないため、提供済みの旧版ソースは同じ検証済みtarから短い`build/d1/SmoothMail`（root長59文字）へ新規展開して標準入口を実行する。これにより既存の監査済みソース識別子・通知・アーカイブの対応を維持する。新しい短縮キャッシュ実装の回帰試験と、旧版の未改変配布ソースによる通し実行は別の検証として記録する。

ソースroot変更の初回再実行はpackage helperのCMake cacheが旧rootを参照していたため停止した（`build/dist01-short-run.log`）。今回新規生成したcacheのうち旧rootを参照する3ディレクトリだけを確認し、削除せず`-previous-source`へ退避した（package、native-runtime-probe、cyrus-sasl-xoauth2）。依存の原本・インストール済みDLL・既存の利用者成果物は削除・上書きしていない。

`build/dist01-run-short.cmd`による再実行を開始した。引数は上記標準コマンドの`-P`だけを`build/d1/SmoothMail/cmake/PackageWindows.cmake`へ変更したものである。新規構築済みの検証付き依存と取得cacheを利用し、標準入口の全ステージを再度通過させる。実行IDは`9479a3323e454172bbf270281ad312a8`、ログは`build/dist01-short-retry.log`。ZIPと後続検査の成功まではDIST-01は未完了である。

## WinFlexBisonの原本汚染と再構築回帰の修正

短いパスでの再実行はWinFlexBison原本のtree pin不一致で停止した。初回の構築と既知tarから独立展開した原本を全ファイル比較し、上流CMakeが原本内`bin/Release`へEXE及びBison dataを32件追加していたことを確認した。既存原文の変更・欠落はなかった。元の生成物入りtreeは`-with-generated-output`として退避し、固定ハッシュ検査を行うimport経路で原本を再展開した。比較用の一度目のtar展開が作業ディレクトリへ出力された際は、今回作成した`winflexbison/`だけを直ちに専用比較ディレクトリへ移動した。利用者ファイルの削除はしていない。

現リポジトリでは、`patches/winflexbison/out-of-source-output.patch`でDebug／Releaseの出力先を`CMAKE_CURRENT_BINARY_DIR`へ変更した。既存の`PreparePatchedSource`で別作業コピーにだけ適用し、原本Git／tar／import済みtreeは変更しない。構築先はパッチハッシュを含む別ディレクトリとし、旧CMake cacheを無理に再利用しない。

実際の固定アーカイブとMSVCで`tests/cmake/verify_winflex_source_isolation.cmake`を実行した。2回連続の構築・インストール成功、両回の原本tree SHA256不変、原本への`bin/Release`非生成、インストール済みwin_flex／win_bisonの`--version`成功を確認した（`build/dist01-winflex-regression.log`）。

パッチを余分な空白なしの表現へ整理した際、一度context不足で適用拒否となったが、前後contextを保持した表現へ訂正した。確定したパッチで同じ2回構築・原本保持・実行試験を再実行して成功した最終ログは`build/dist01-winflex-regression-checked.log`。適用検査やtree pinの条件は変更していない。

確認済み旧版ソースの通し検証は、固定原本を復元して未改変の標準入口から再実行している（`build/dist01-short-final.log`、実行ID `70b5e41d90b54c19b57b1c06945795ea`）。旧版の生成物を無視するようtree pinを緩めたものではない。新実装の反復構築回帰と、旧版の最終入力での通し検証を区別する。
