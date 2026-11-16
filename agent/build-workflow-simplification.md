# ビルド・配布入口の単純化

対象: `3d4d457`の`spec/distribution.md`・`spec/docs.md`。併せて`4e6a91c`のビルド／バッチ分離と`spec/tech.md`を維持する。

## 構成

- `debug`／`release` presetはNMakeを外側の薄い入口に使用する。NMakeは許容されるMSVC開発SDKに含まれ、Ninjaの事前導入を要求しない。
- コンパイル・依存準備・最終再リンクは`cmake/RunBuildWorkflow.cmake`からCMakeのみで実施する。Pythonへコンパイラ設定や本体のビルド処理を移さない。
- ランタイム配置・ライセンス／ソース収集・通知生成・圧縮はPython。Debug配置も`tools/stage_windows_debug.py`へ移し、旧CMake配置入口は互換用途のため保持する。
- `python tools/build.py dist`（26文字）はRelease presetの構成とビルドを呼ぶ配布用の短い入口。通常の2行Release手順と処理を共有する。
- Releaseはbootstrap→原本／対応ソース／通知生成→通知埋込み最終リンク→最終証拠の再収集→既存C++補助の検査／ZIP生成→自動展開・全バイト照合とする。
- 草案の既定動作は変更しない。`generate_licenses.py --final-review --distribution`で完全性検査を伴う配布資料を新規生成する。配布用manifestは法的監査完了を表すものではない。草案を手で改名して承認する方式は標準手順から外す。
- `.env`の構築パスは通知／OAuthメタデータへ渡さず、実`.env`もソースアーカイブへ収録しない。既存実`.env`は変更しない。
- ビルド中の`.env`変更を検出する。Releaseでは生成ソースのcommitと追跡ファイルのクリーン状態を最終梱包直前にも照合する。
- ジョブ固有の新しい出力へ生成し、前回成功した実行／配布出力を退避する。既存の依存キャッシュ・ユーザ資料を削除しない。

## 文書

- `docs/dev/build.md`はDebug／Release各2行のみ。構成33／35文字、ビルド23文字でビルド先を明示する。
- `docs/dev/run.md`は各1行24文字。ビルド時配置済みの`run/launch.cmd`を使う。
- `docs/dev/distribution.md`は`.env`全項目、自動生成、生成後の監査、公開外の維持責任、必要な公開物・同じページでの公開・CRT条件表示を分離して記載。
- `docs/dev/test.md`の内部アプリ構築先を`build/dbg/app`へ合わせる。

## 検証

- MSVC 2022の実開発者環境でDebug／Release presetのconfigure成功。
- 既存実`.env`には新しいQtパス項目がないため、標準Debug buildが明示的な設定案内で停止することを確認。実`.env`に勝手な値を追加していない。
- Python試験44件成功（任意の文書生成依存未導入による8件skip）。コマンド長・手順行数・構築設定非混入・Debug配置／推移依存／SDK非改変・生成／監査分離・対応ソース再照合・出力退避／失敗復元・ZIP全バイト一致・不正パス／大小文字衝突拒否・日本語のツール出力パスを検査した。
- CMakeのビルドガード、ライセンス環境／テンプレート16件、配布入口9件が成功。
- MSVCで配布補助を別途構築し、CRT実バイナリ・長いパス・ソースアーカイブ・通知・ZIP・Pythonを含むCTest全12件が成功。最初の補助構成は検証コマンド内の`%PATH%`早期展開でNMakeを失ったが、`cmake -E env --modify`によるPATH追加へ修正して成功した。
- clean cloneへ検証専用`.env`だけを作成して`python tools/build.py dist`を実行し、Ninjaの自動取得・実コンパイルと固定vcpkgの取得を確認。CMake HTTPSのstatus 35には、文書どおり`.env`へOpenSSL対応curlを指定して再実行し、vcpkgの構築ツール・GNU／MSYS2取得へ進んだ。実運用の`.env`・Qt SDKは変更していない。
- 製品全体のclean cloneからの取得・Debug／Release構築・SDKなし実行・改変DLL差替えは、この時点では未検証。BATCH-01で管理し、個別試験や既存旧入口の成功を通し検証済みとは扱わない。

## 反復実行時の保護

- 異なるDebug／Releaseの構築先から同じ依存rootへ同時に書き込まないよう、CMakeのプロセス寿命の排他ロックを依存rootにも設ける。
- Debugランチャーは配置内のQt／SASL探索先を明示し、ホストの別SDK向け環境設定を引き継がない。専用fixtureでランタイム一式の配置とSDK原本非改変を確認した。
- CMakeがUTF-8で書き出すCRT検証器のパス記録は、PythonでもUTF-8を明示して読む。Windowsの既定コードページへ依存しない。

## Debug配置の実DLL試験

- `tests/debug-runtime/`の小さなQtアプリと合成のSASL名DLLを、実SDK／MSVCのDebug設定で構築した。
- 新Python配置処理がQtの推移DLL、明示プラグイン、Debug CRT／UCRTを収集できた。
- SDK／依存のPATH・プラグイン環境を除去した子プロセスで、SQLiteのインメモリDBとTLSバックエンドのロードが成功した。ネットワーク、永続DB、資格情報ストアを使用しない。
- この試験は`tests/cmake/run_debug_staging.cmake`で再実行でき、MSVC環境での配布補助CTestにも登録する。本体／実SASL／SDKなしOSの試験とは区別する。
- 登録後の配布補助CTest全13件が成功した。

## clean cloneのRelease通し実行で判明した問題

- 最初の通し実行は、全依存・本体・対応ソース・通知・最終リンクまで完了し、ZIP検査で停止した。Qt SDKの10個のポリシー対象はすべて既知ハッシュと一致したが、自前で構築した`libsasl.dll`だけが旧ポリシーの全体ハッシュと不一致だった。
- 自前ビルドのDLLを毎回レビューして静的許容ハッシュへ追加する方式では、新規cloneからの単一コマンド生成を安定して達成できない。バイナリ検査の緩和・自動追認・事後書換えはしない。
- Cyrus SASL 2.1.28のWindows設定にある不要な絶対PLUGINDIR／CONFIGDIRを空値へ変更する限定ソースパッチを追加した。アプリ・既存テストは初期化前に`sasl_set_path`で配置内プラグインパスを明示しているため、その経路は変更しない。
- 固定原本tarは保持し、パッチdigestをキーに別の構築用コピーを作る。反復利用時の設定digest照合により、変更された設定を上書きしない。変更通知・英日テンプレート・対応ソースのパッチ検査も追加した。
- SASL用のパス許容エントリを削除し、以後は通常の厳格検査で扱う。Qt SDK向け限定ポリシーは変更しない。
- 実SASL Release DLLを再構築し、例外なしの`--audit-stage`検査が成功。実DLLのPLAIN／LOGIN／XOAUTH2等のQt Testは7 passed、0 failed、0 skipped。原本保持・パッチ分離・再利用・改変拒否fixtureとPython44件の試験も成功した。
- SASL fixture登録後の配布補助CTestは全14件成功。Release生成で固定したQtソース／供給元ポリシーと整合するよう、通常手順の対象SDKをQt 6.12.0 `msvc2022_64`と明記した。
- `3529268`を検証用cloneへ反映し、前回出力や原本を削除せず、修正後のReleaseワークフローを再実行している。
