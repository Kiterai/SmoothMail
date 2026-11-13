# Windowsビルド体制移行

## 2026-11-14: 取得処理

- CMake 3.30のTLS検証付きdownloadとSHA256/SHA512照合、検証済みキャッシュ、排他・非上書き公開を追加。
- vcpkg資材取得をCMake x-scriptへ統一（origin fallbackを禁止）。システムcurl・Python helperを使用しない。
- Ninja 1.13.1を上流タグで確認したcommit `79feac0f3e3bc9da9effc586cd5fea41e7550051`からGit取得、MSVC付属NMakeで構築。Pythonなしでホスト構築成功。
- オフラインdownload guard 4ケース成功。
- CMakeからGitHub release ZIP取得を実試行したところstatus 35 (SSL connect error)。TLS検証を無効にしない。Git HTTPSのcommit取得は成功。
- 全体移行と実Release配布・監査は別工程。生成スクリプトの実装だけでDIST-01/02を完了扱いにしない。
