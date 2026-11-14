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

## KConfigの承認済みパッチ（2026-11-14）

- `patches/kconfig/6.31.0-optional-check-update.patch`を作成。既定ONの機能選択を追加し、OFF時は絶対helper定義を生成せず、公開checkUpdateは固定警告だけを出す。設定変更・再読込・移行済み記録・外部プロセス起動をしない。原著作権/LGPL表記を保持し、変更者/日付/目的を各変更ファイルに明示。
- 正式な経緯・理由・ライセンス/保守説明は`patches/kconfig/README.md`、配布向け要約は`THIRD_PARTY_NOTICES.md`。以前の調査メモの「未採用」は調査時点の記録として残す。
- `BuildKconfigDependencies.cmake`を追加し、Framework bootstrapのWindows経路へ統合。独立patched-srcとpatchハッシュ別binaryを使い、Debug/ReleaseともOFF。生成ヘッダとcompile commandsを検証してからreceiptを発行する。
- Debug/Releaseの構築・インストール、アプリの両構成の再構築成功。別binaryの既定ONビルドも成功し、設定ヘッダ1/絶対helper定義の維持を確認。ON版のcheckUpdateは実行していない。
- OFF/ON DLLの`dumpbin /exports`で同じcheckUpdateシンボルを確認。OFF DLLを読む実行前ガード付きkdebase試験を追加し、Debug/Releaseで成功。テストは合成設定ファイルだけを使用し、未保存値・dirtyフラグ・ディスク内容・移行記録の非変更と固定警告を確認する。
- ホスト18 CTest対象成功。Windows向け合成configure fixtureを追加し、KConfigのreceipt一致は受理、不足/置換は拒否する3ケースも成功。
- 検証用runtimeは`build/kconfig-patch-release-runtime`。KConfigCore/Guiは検査通過。全体検査の次の拒否箇所はKF6Crash.dll。個別検査では15エントリが拒否（KIOCore、Qt、GNU/SASL等も含む）。未分類の固定パス/誤検出もあり、全てをビルド者情報と断定しない。検査は緩和せず、ZIPは公開していない。
- テスト中に共有Ninjaを二重構築してobj生成が競合したため、bootstrapをfunction lockで直列化。修正後に同時起動した2回のPrepareBuildToolsがともに成功（6d58a8e）。
- Qt SDKと通常起動先runtimeは変更せず、実サービス・実設定・資格情報へアクセスしていない。DIST-01/02/03および他DLLの検査は未完了のまま。
