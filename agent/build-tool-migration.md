# Windowsビルド体制移行

## 2026-11-14: 取得処理

- CMake 3.30のTLS検証付きdownloadとSHA256/SHA512照合、検証済みキャッシュ、排他・非上書き公開を追加。
- vcpkg資材取得をCMake x-scriptへ統一（origin fallbackを禁止）。システムcurl・Python helperを使用しない。
- Ninja 1.13.1を上流タグで確認したcommit `79feac0f3e3bc9da9effc586cd5fea41e7550051`からGit取得、MSVC付属NMakeで構築。Pythonなしでホスト構築成功。
- オフラインdownload guard 4ケース成功。
- CMakeからGitHub release ZIP取得を実試行したところstatus 35 (SSL connect error)。TLS検証を無効にしない。Git HTTPSのcommit取得は成功。
- 全体移行と実Release配布・監査は別工程。生成スクリプトの実装だけでDIST-01/02を完了扱いにしない。

## 依存統合・Python移植

- `BuildWindowsDependencies.cmake`を追加しNative→Framework→WinFlexBison→I18n→Solid→KIO→SASL→XOAUTH2→Keychain→Mailを独立プロセスで構築。Debug一式の成功確認済み（既存ソース/ダウンロードキャッシュあり）。
- `BuildWindows.cmake`でアプリまで構築する。gettextホストツールは明示指定し、システムPATH準備責務にしない。
- `PackageWindows.cmake` + QtCoreだけを使うC++配布補助へ置換。ZIPの生DEFLATE、固定日付・権限、CRC32、SHA256 inventory、排他的hardlink公開を実装。CMake/libarchiveで独立展開・全inventory照合成功。
- Pythonテスト5本をCMakeガードとC++自己テストへ移植。手動Google補助2本も明示オプトインC++へ移植し、ビルド・CTestからライブ実行しない。
- アプリと手動プローブのMSVC Debugビルド成功。ホスト18 CTest対象成功（従来15 + 配布系3）。実サービス・資格情報書込み・ループバック依存3対象は実行していない。
- MSVC 19.44で従来の`/pathmap`がD9007で無視される問題を検出。Releaseのapp/依存/SASLへ`/experimental:deterministic`を追加。初回Release依存ジョブを明示停止し、修正版で再構築。
- 修正版のRelease依存一式とアプリ構築成功。D9007は0件。テストオプションに依存して偶然導入されていた`KF6::I18n`を本体側の明示find_packageへ修正し、テストOFFのRelease生成成功。dev presetにも内部gettextツールを明示。
- MSVC開発環境からのRelease CRT探索とStageWindowsRelease完走。`build/release-runtime-check`は検証用で通常起動先とは別。
- 実ランタイム監査は`KF6ConfigCore.dll`のUTF-16の絶対prefix（設定更新補助EXEへのパス）で停止。生のプライベートパスは検査診断に表示せず、ZIPも公開していない。原因は上流`src/core/CMakeLists.txt`の`KCONF_UPDATE_INSTALL_LOCATION`。パスマッピングだけでは消えないためDIST-04へ記録し、検査を回避しない。
- legal一式の監査未完了。実製品ZIP・クリーンWindows起動は未検証。ホストの実メール・資格情報・登録JSONは参照せず、Googleライブプローブも実行していない。
