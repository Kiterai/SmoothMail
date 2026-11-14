# Git管理情報なしの依存アーカイブ構築

2026-11-15。[初回DIST-02検証](dist02-source-rebuild-replacement.md)で見つかった、提供済み依存アーカイブの直接利用経路を整備。

## 実装

- 明示的な`SMOOTHMAIL_SOURCE_ARCHIVES`でアーカイブ入力へ切り替える。通常の固定Git取得経路は変更せず、archiveモードからGit／ネットワークの依存ソース再取得へ暗黙フォールバックしない。
- `cmake/source-archives.json`に、19件の固定commit・配布用tar SHA256・展開後tree SHA256を記録。依存のcommitは`dependencies.json`と一致させる。SHAを入力側のmanifestだけに委ねず、プロジェクト側のpinと照合する。
- tree digestは、ソートした全相対ファイル名と各SHA256を連結したUTF-8文字列（各行末LF）のSHA256。隠しファイルを含み、リンク・`.git`・不適切な名前を拒否する。ファイルの追加・削除・変更を再利用時にも検出する。
- pristine sourceは`DEPENDENCY_ROOT/archive-src`。パッチ作業コピーは別ディレクトリへ作り、再利用時は固定pristineからパッチを新規適用した期待treeと比較する。改変された既存コピーは上書きしない。receiptや同梱manifestの書換えで改変を正当化しない。
- patch適用時はGitの親repository探索を停止し、Git環境のDIR/WORK_TREE/INDEX_FILEを除去する。偽のcommitや`.git`を生成しない。
- vcpkgの実行工具・downloads・buildtrees・packagesはpristine外へ配置。GNU/Cyrusの原本は提供アーカイブから既存のハッシュ検証を通して使う。外部工具キャッシュは`SMOOTHMAIL_ASSET_CACHE`で指定でき、工具自身のpin検証を維持する。
- 収集対象へSolid／WinFlexBisonを追加し、23アーカイブと19 pristine treeの検査へ更新。ソースREADMEへ実際の構築コマンドと、外部工具／Qt SDKの前提を記載した。
- `BuildWindows.cmake`から`SMOOTHMAIL_SOURCE_COMMIT`をアプリconfigureへ引き渡す。QtKeychainの通知収集・legalレビューもarchive sourceの配置に対応。
- `BuildQtSourceReview.cmake`は明示的な任意検証用。独立prefix・固定Release/Schannel/内蔵SQLite設定・worker prefixのpathmap・PDB論理名を使用し、既存SDKの配下／祖先を出力先として拒否する。構築設定とsource/compiler hashをレビュー資料に残す。

## 初期ガード検証

`run_source_archive_guards.cmake`の12ケースと、download/source-cacheガード7ケースに成功。既存のpatch/receipt/stagingガードも維持。MSVC開発者環境で配布補助CTestの7対象すべて成功（`build/d03-package-tests-final.log`）。

最初の配布補助試験は、CMDの同一行で`vcvars`を呼んだ後に事前展開済み`%PATH%`を設定したためnmake探索に失敗。CMake内で実行時の環境へQtを追加して再実行し成功した。検査を無効化する変更はしていない。

全23アーカイブの新規出力と、提供物を使う標準入口からの全再構築・差替えは、この実装をコミットしたソースを新規収集して別途検証する。機能追加だけをもってDIST-02完了とは扱わない。

後続検証完了: 最新提供物から標準再構築・独立Qt構築・同一EXEでの改変DLL差替えに成功。最終Sandboxは両構成各247成功・失敗0・スキップ0。[最終結果と証跡](source-rebuild-regression-followup.md)。以下のガード修正も含めた提供物を検証した。最終legal付き製品ZIPの承認とは別である。

後続ガード修正: Windowsで`cmake_path(IS_PREFIX)`がパスの大小文字を同一視しないことを再現。既存SDKと出力の既存ancestorをcanonicalizeし、大小文字を畳んでSDK配下／祖先を拒否する専用関数へ分離した。独立出力・同一・子・親・大小文字違い・`..`正規化の6ケースを追加し、SDKを変更せず検証する。
