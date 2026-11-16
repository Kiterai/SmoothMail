# 構造資料と依存図生成

対象仕様: `f704c68`、`spec/docs.md`。

- `docs/dev/architecture.md`に現行モジュール、直接／推移依存、開発ツールを整理した。
- `tools/generate_architecture.py`はPython標準ライブラリのみ使用。構築済みCMakeのGraphviz出力と、`src/`限定のDoxygen XMLを入力に3種類のDOT／SVGを生成する。ビルド工程には接続しない。
- CMakeテスト／probe／配布補助ターゲットを除外し、内部／外部ノードを色分けする。ファイル図は外部includeを含み、ディレクトリ図はプロジェクト内だけを集約する。
- Python単体試験3件成功。既存Release構築設定を使い、CMake 3.30.0、Doxygen 1.18.0、Graphviz 16.1.1で実生成を確認した。生成時に本体のコンパイル／認証／GUI操作は実行していない。
- 文書用ツールは公式配布のZIPを`build/doc-tools/`へ展開して使用し、リポジトリへバイナリは追加しない。Doxygen SHA256: `e84f54cfd49ef06b0b16536056dbec0c496323de28abcce53a4269463de35eaf`、Graphviz SHA256: `72b1f3a9a1fd3f6e726478c1e044a95483a8fbddec13ba2fb395438d75d001b4`。Graphvizは同配布元の`.sha256`とも照合。
- 取得元: <https://github.com/doxygen/doxygen/releases/tag/Release_1_18_0>、<https://graphviz.org/download/>。図自体は当プロジェクトのCMake／ソースから自動生成したもので、外部の図素材は使用していない。
