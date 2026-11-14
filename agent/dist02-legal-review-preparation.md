# DIST-02事前監査資料の作成

2026-11-14。利用者依頼により、統合Release候補のlegal資料を作成。

## 作成・確認済み

- `docs/dev/distribution-legal-audit.md`: 39ファイル/19コンポーネントの構成、ライセンス根拠、固定ソース、変更提供、差替え、未確認事項。
- `cmake/PrepareWindowsLegalReview.cmake`: 実runtimeのSHA256と一意な部品対応、原文ライセンス・著作権・固定ソース案内・既存パッチ説明を新規出力先へ収集。
- 実行結果: `build/legal-windows-review`に134ファイルを作成。原文はコピーし、再ライセンス・免責省略はしない。
- KDEの原本LICENSES集合とソース著作権行は収集したが、全LICENSESが同梱DLLへ一括適用されるという判定ではない。
- Qt SDKのSBOMからqtbase/qtsvgの完全なソースcommit識別子を確認。元SBOMには供給元のローカルパス等があるため、そのまま製品legalへコピーせず、資料へ必要な識別情報だけを記録。
- Cyrus SASLの必須CMU謝辞を部品別通知へ追加。
- 39ファイルはすべて一意に割当済み。原文・通知の参照存在を検証。
- `manifest.json`は作成せず、未承認の`manifest.review.json`だけを生成。Qt Base/Qt SVG/CRTの不足を空のlicense_filesとして明示。
- `tests/cmake/verify_windows_legal_review.cmake`で、部品数・ファイル数・参照と、レビュー用フォルダの配布入力拒否を確認。
- 自分の新規fixtureだけへdraftをmanifest.jsonとしてコピーしても、空のQt/CRT原文配列により`--check-legal`が拒否することを確認。
- 同じ出力先での再生成を拒否し、元manifest.review.jsonのSHA256非変更も確認。

## 未完了・判断が必要

配布責任者へ確認すること:

1. Qtの採用条件（オープンソース条項か、権限のある商用契約か）。契約書やキーのチャット/Gitへの開示は不要。
2. MSVC CRTの再配布権限と、対象10ファイルのREDIST確認。
3. 完全な対応ソースの提供方法・具体的な提供先・保管責任。未履行の書面オファーや架空URLを作らない。

引き続き必要な技術監査:

- Qtと一致する原文・完全ソース、実際に組み込まれた第三者コードの通知。
- KDE/GNU等のコンパイル/リンク対象・ヘッダ/生成物と許諾条件の最終照合。
- LGPL等の採用条項に沿った改変DLLの差替え確認、必要に応じたInstallation Information等。
- legalを確定した実ZIPのレビュー・独立展開/ハッシュ照合・クリーン環境確認。

DIST-02全体を完了とはしていない。DIST-04のruntime検査は別途完了。
製品ZIP公開、SDK/通常起動runtime変更、実認証や実メール操作は行っていない。
