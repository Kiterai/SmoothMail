# ビルド以外のバッチ処理のPython移行

対象: `28ca959`、`7d7b3f1`、`spec/tech.md`。

## 構成

- コンパイル・依存構築・ビルド準備はCMakeのまま。Pythonを本体ビルドへ組み込まない。
- 配布入口は`tools/package_windows.py`、Release配置は`tools/stage_windows_release.py`。
- ライセンス／対応ソース収集は`tools/collect_legal.py`、`tools/collect_sources.py`。
- テンプレート生成は`tools/generate_licenses.py`。既存原文は未整形でコピーし、草案／最終表記とも`review_only`を維持する。
- Pythonは標準ライブラリのみ、3.11以降。バッチ用のプラットフォーム依存シェルスクリプトは追加しない。
- C++のバイナリ監査・CRT署名検証・決定的ZIP出力は独立検証済みのネイティブ補助を維持し、Pythonから呼ぶ。補助のコンパイル専用入口は`cmake/BuildDistributionTools.cmake`。
- 旧CMakeバッチ入口は移行案内付きで停止する。CMakeのLicenseTemplates／LicenseFinalReview／QtLegalAttributionsは既存回帰テスト用であり、新しい配布入口からは呼ばない。LicenseResourcesはビルドに必要なリソース埋込みのため維持する。

## 保持する検査

- 配布前の存在・通常ファイル・出力非上書きチェック、原本ハッシュとソースtree digest、対応ソースとバイナリcommitの一致。
- 通常／遅延importの依存解決、明示したディレクトリ以外からのDLL混入拒否、Debug CRTと未承認CRTの拒否。
- CRTの署名・x64・版・原本一致は従来の検証補助で配置前後に確認する。
- 監査補助による全DLLのライセンス対応、個人情報／ビルド情報のバイナリ監査と排他的ZIP公開を維持する。
- Qt原本の`qt_attribution.json`には文字列内の生改行がある。CMake JSONの既存挙動に合わせ、このメタデータだけ`strict=False`で読み、原文ファイルは変更しない。他のmanifestのJSON解析は緩めない。
- Windowsの短縮パスと長いパスの混在に対応するため、原本の比較・相対化前にパスを正規化する。
- dumpbinのimport名は小文字化して探索するが、配置ファイル名は原本の実際の綴りを保持する。Windows上では同じファイルでも、manifestとZIPの相対名の文字列比較には大文字小文字が影響するため、実配置で発見した不一致を修正し回帰試験を追加した。

## 検証

- Python単体試験は依存図・配布入口・DLL解決・dotenv／テンプレート・草案生成・原本保存・source pin／tar traversal・Qt attributionを対象とする。
- 既存配布補助のCTest 11件成功（MSVC環境を維持して実行）。最初の実行はテストコマンド側でPATHを上書きしてNMakeが消えたためCRT 2件が失敗したが、環境を修正した全件再実行で成功。
- Pythonの新Release配置を実バイナリで実行し、必要なCRT 5 DLLの配置前後検証とwindeployqtを確認した。既存SDKは変更していない。
- 既存のアーカイブモード依存rootを、通常Gitモードのビルド補助へ渡すとNinjaのソースパス不一致で停止した。キャッシュは削除・改変せず、専用の新しい補助ビルドrootで検証した。配布入口では`--source-archives`を構築にも引き継ぐ。
- 製品ZIPの再承認・公開、実アカウントの再認証は実施していない。
- Python単体試験15件成功。新配布入口の製品ZIP通し検証は`spec/inprogress/batch-migration.md`で別途管理する。

## 実データによる収集・生成の確認

- 新しい配布補助ビルドのCTestはPython試験を含め12件すべて成功した。
- 正しい綴りのDLL名で再配置したruntimeから19コンポーネントのライセンス証拠を収集できた。対象全34ファイルとmanifest割当が一致した。
- `4e6a91c`を対象に対応ソース23アーカイブを生成した。独立したCMake検証で全アーカイブの展開・ハッシュ、19原本treeの固定値、7つの有効KDEパッチの適用を確認した。利用手順・新Python入口・構造資料が含まれ、実`.env`と`.agent-config/`を含まないことも確認した。
- 実際の証拠と対応ソースから英日草案を生成し、C++補助の`--check-legal-review`が成功した。生成先の原文267ファイルは収集原本とSHA256がすべて一致した。
- 新しいPythonのruntime証拠検査で全割当・全ファイルを確認し、実CRT 5 DLLの署名・版情報`14.44.35211.0`を確認した。
- Release配置CLIにも`--source-archives`等を追加し、アーカイブモードの既存依存rootで補助構築と配置が成功することを確認した。通常モードへ暗黙に戻すことはしない。
- これらは通知の配布承認や、変更後の製品ZIPをクリーン環境で再実行したという意味ではない。BATCH-01は維持する。
