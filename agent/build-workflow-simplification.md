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
- Python試験42件成功（任意の文書生成依存未導入による8件skip）。コマンド長・手順行数・構築設定非混入・Debug推移依存・生成／監査分離・対応ソース再照合・出力退避／失敗復元・ZIP全バイト一致・不正パス拒否を検査した。
- CMakeのビルドガード、ライセンス環境／テンプレート16件、配布入口9件が成功。
- 製品全体のclean cloneからの取得・Debug／Release構築・SDKなし実行・改変DLL差替えは、この時点では未検証。BATCH-01で管理し、個別試験や既存旧入口の成功を通し検証済みとは扱わない。
