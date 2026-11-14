# DIST-04: ビルド設定による回避可能性の調査

調査日: 2026-11-14。KConfig/ECM 6.31.0、Qt 6.12.0、MSVC 19.44、CMake 3.30。

## 結論

- 現在のKConfig DLL構成を維持し、上流が提供する通常の設定だけで「ビルド者のパスなし・ポータブル・更新処理を維持」を満たす設定は見つからない。
- **静的KConfig + 関数単位最適化 + 未使用コード除去**には実験上の見込みがある。未使用の`KConfig::checkUpdate()`が消えるため、その文字列も最終EXEから消える。ただし依存一式の静的化/混在構成を採用したという意味ではない。
- 製品ビルド設定・上流ソース・既存prefix・検査ルールは変更していない。DIST-04は未完了のまま。

## 原因と設定の制約

- KConfig `src/core/CMakeLists.txt:58-61`はターゲットのコンパイル定義として
  `KCONF_UPDATE_INSTALL_LOCATION="${KDE_INSTALL_FULL_LIBEXECDIR_KF}/$<TARGET_FILE_NAME:KF6::kconf_update>"`
  を直接追加する。同名のCMakeキャッシュ変数を参照する構成ではない。
- ECM `kde-modules/KDEInstallDirs6.cmake:205-209`はWindowsで
  `_define_non_cache(LIBEXECDIR_KF "${CMAKE_INSTALL_LIBEXECDIR}")`を呼ぶ。
- ECM `KDEInstallDirsCommon.cmake:223-230`はその値が相対なら`CMAKE_INSTALL_PREFIX`を付加する。`KDE_INSTALL_FULL_LIBEXECDIR_KF`をキャッシュで指定しても通常変数で上書きされる。
- KConfig `src/core/kconfig.cpp:557-568`はその定義を`QStringLiteral`にして`QProcess::execute()`へ渡す。`__FILE__`ではないので`/pathmap`の対象ではない。
- `checkUpdate()`は非推奨APIの無効化マクロで囲まれていない。上流の公開optionはGUI/QML/DBus等で、この処理の無効化・再配置用optionは見つからなかった。
- 実Release DLLの`dumpbin /exports`で`KConfig::checkUpdate`のexportを確認。DLLの公開APIなので、通常の`/OPT:REF`だけで未使用扱いにして削除できる前提にはできない。

## configure実験

上流ソースを変更せず、個別の新規binaryディレクトリでconfigureして`build.ninja`の実コンパイル定義を確認した。インストールは行っていない。

| 指定 | 実際に定義された場所 |
|---|---|
| 標準設定 | `<実験prefix>/bin/kconf_update.exe` |
| `-DKDE_INSTALL_FULL_LIBEXECDIR_KF:STRING=relative-helper` | 標準と同じ。上書きされる |
| `-DKDE_INSTALL_LIBEXECDIR:PATH=helpers` | `<実験prefix>/helpers/kconf_update.exe`。相対化されない |
| `-DKCONF_UPDATE_INSTALL_LOCATION:STRING=kconf_update.exe` | 標準と同じ。ターゲット定義には反映されない |
| `-DCMAKE_INSTALL_PREFIX=C:/SmoothMail` | `C:/SmoothMail/bin/kconf_update.exe`。固定の絶対パスへ変わるだけ |

固定prefix + `cmake --install --prefix <実際の配置先>`やstagingは、コンパイル済み文字列を書き換えるものではない。特定フォルダへのインストール方式なら別途設計可能だが、任意場所へ展開するポータブルZIPの解決にはならない。現検査も固定の絶対パスを許可する設計ではない。

EXE名だけや相対パスへのコンパイル定義の強制差替えは、カレントディレクトリ/PATHから意図しないプログラムを拾う危険と機能変更を伴う。CMakeのDEFER/hookでターゲット定義を後から書き換える方法も、上流の標準オプションではなく実質的な変更として監査すべきである。

## 静的リンクの小実験

実験ファイルは`build/dist04-investigation/`。既存の依存インストール先からECM等を参照し、上流KConfigを別binaryツリーで静的構築した。

- `BUILD_SHARED_LIBS=OFF`、Release、`/Gy`、リンク時`/OPT:REF /OPT:ICF`。
- ソース記録用には現在のRelease構築と同様に`/experimental:deterministic /pathmap`、PDBには`/PDBALTPATH`を使用。
- 合成の最小EXEでKConfig/KConfigGroupを使用し、`checkUpdate()`は参照しない。
- **結果:** 最終EXEは既存C++補助のPE/個人パス検査を通過。DLL依存にはQt6Coreがあり、KF6ConfigCoreはない。
- **対照:** 同じ静的ライブラリを使う別EXEから`checkUpdate()`も参照すると、個人パス検査は`Embedded input directory detected`で拒否した。単にパスマッピングが直したのではなく、不要な処理の除去による結果である。
- **安全上の区別:** いずれの合成EXEもリンク・バイナリ検査だけで、実行していない。実設定の読書き・更新補助EXEの起動はしていない。
- KConfig原本のtracked変更がないことを確認。

## 静的案を採用する前に必要なこと

1. 実際のKIO/KService/KIMAP/KSMTP等を再構築し、最終EXEだけでなく全同梱DLLで文字列が消えるか確認する。最小EXEの成功を製品全体の成功へ拡張しない。
2. KIOCoreとKServiceはそれぞれConfigCoreへリンクする。KConfigには`ksharedconfig.cpp:46-57`等のプロセス内共有を意図したstatic/thread storageがあるため、複数DLLへの静的組込みによる状態の重複・型/API境界への影響を検証する。
3. 一部静的化か依存群の静的化かを設計し、変更済み依存receipt・ターゲット/import/export・DLL収集・ライセンスcoverageの扱いを更新する。Qtの再導入/静的化は前提にしない。
4. LGPL等の差替え/再リンク手段・提供物・通知を再検討する。静的リンクが一律に禁止という意味ではないが、現行のDLL差替え前提と同等とは扱わない。

従って「設定だけなら可能性が全くない」とは言えないが、現行の動的リンク構成に対する小さな設定修正では解決しない。構成維持が優先なら、上流の再配置対応または安全な限定パッチの方が変更範囲を局所化しやすい。
