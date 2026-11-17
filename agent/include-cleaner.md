# 不要 include の機械的調査・除去

## 対象と方法

- 2026-11-18、`misc-include-cleaner`で調査。VS同梱clang-tidy 19.1.5を使用し、23.1.4でも修正後を再確認した。
- 本体・通常テストに加え、GUI smoke、Google手動probe、SASL／KDE／KIO／通信／native-runtime、配布補助のオプションを有効にした解析専用Debug／Release構成を`build/include-cleaner/`へ作成。既存配布出力や`.env`は変更しない。
- 各構成のコンパイルデータベースに含まれる自前C++ソース53件、独立したsource-replacement／debug-runtime／CRT検証器の4ソース、および自前ヘッダー38件を解析した。依存ソース・生成コードは修正対象外。
- Qtの`QLoggingCategory`等の転送ヘッダーはinclude guardがなく、そのままでは不要includeの検出対象から外れる。解析用ソースコピーだけで、単一の`IWYU pragma: export`を持つ転送ヘッダー名を同じ内容の実体ヘッダー名へ正規化した。元のinclude表記・Qt SDKは変更していない。解析コピーには元ファイルのディレクトリをincludeパスとして追加した。
- ClangによるWindows SDKの`offsetof`解析には`/D_CRT_USE_BUILTIN_OFFSETOF`を解析時だけ追加。通常のMSVCビルド設定には追加していない。
- Debug／Releaseで一致する不要include診断の削除修正だけを元ファイルへ反映。不足includeの追加や集約ヘッダーの分解は行わない。

## 除去した5件

| ファイル | include |
|---|---|
| `src/core/diagnostics.cpp` | `QLoggingCategory` |
| `src/ui/mainwindow.cpp` | `QSplitter` |
| `tests/manual/google_credential_probe.cpp` | `QDir` |
| `tests/tst_mainwindow.cpp` | `QStyledItemDelegate` |
| `tests/tst_messageviewer.cpp` | `QScrollBar` |

## 残した診断

- `windows.h`、`QtTest`は使用するAPI／マクロを提供する集約ヘッダーなので残した。
- `diagnostics.cpp`の`crtdbg.h`はReleaseでは不要と判定されるが、DebugのCRTフックで必要なので残した。
- `aboutdialog.cpp`の`QJsonArray`は19.1.5／23.1.4とも不要と判定するが、`document.array()`を使うrange-forで完全型が必要。削除後のMSVCビルドでC2027等を確認し、元に戻した。型名がソースに現れない利用には注意する。

## 検証

- 修正後のオプション有効Debug／Release全ターゲットのMSVCビルド成功。
- 通常14ターゲットとオフライン`google_probe_lifecycle`のCTest各15ターゲット成功、計30ターゲットで失敗なし。
- 資格情報ストア書込み・意図的クラッシュ等のopt-inは有効にしていない。実Google認可・実メール操作・guest専用GUI試験や通信追加試験は実行していない。
- 修正後の53ソースを両構成で再解析し、残る不要include診断は上記の保留対象のみ。
- ローカルの解析スクリプト、コンパイルデータベース、修正候補YAML、ビルド／テストログはGit対象外の`build/include-cleaner/`に保存。主要ログは`debug-tests.log`、`release-tests.log`、`debug-build-after.log`、`release-build-after.log`。解析結果は`debug-after/`、`release-after/`。
