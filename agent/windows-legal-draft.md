# 配布物向けlegal文面案

2026-11-15。利用者依頼「配布物用のlegal記述を作ってください。完成後にこちらでレビューします。」への作成記録。承認・製品ZIP生成・公開は行っていない。

## 文面と入力

- `legal/windows/`: レビュー入口、利用許諾・第三者通知、対応ソース／再構築／DLL差替え、変更通知、Microsoft CRT限定の追加条件案。
- `cmake/PrepareWindowsLegalDraft.cmake`: 原文収集結果を変更せず、新しい出力へ文面と対応表を組み立てる。常に`review_only: true`、`manifest.json`を生成しない。
- 実入力は`build/d05-legal-baseline-review`と`build/d05-source-review`。改変export入りテストDLLではなく、通常構成の34ファイル／19コンポーネントを基準とする。
- `build/windows-legal-draft`: レビュー用一式。`README.md`から4文面、`COMPONENTS.md`、`SOURCES.md`、各`notices/`、271件の原文・著作権資料へ移動できる。
- Qt SDK SBOMの既存レビューから63依存の著作権／上流条件表記を補助一覧へ展開。依存閉包を実コンパイル対象の認定とは扱わず、原文・attributionを保持する。

## Microsoft条件

公式のVS 2022 Community契約と2015–2022 Runtime契約の掲載原本を確認。取得URLと原本SHA256は`legal/windows/README.md`に記録。公式ページの単体Runtime契約には再配布を許可しない条項があるため、それを再配布権の根拠にしたり、SmoothMail全体の契約へ一括転記したりしない。

配布者の権利は適用Visual Studio契約・REDIST一覧による。CRTだけの保護条件案を作り、使用範囲・通知保持・再配布・解析例外・法令・免責／責任制限を整理した。OSS部分の改変・差替え・必要なデバッグの権利と強行法上の権利を妨げない。契約同等以上の保護になるかと利用者／再配布者の同意取得方法は、利用者レビューに明示して残す。MSVC資格の追加承認待ちを新設したものではない。

## 検証

`tests/cmake/verify_windows_legal_draft.cmake`で以下を確認し成功：

- 19コンポーネントの版・binary対応が入力と一致し、34ファイルの重複なし。
- 271件の原文ファイルが収集元と全SHA256一致。
- 編集対象文面・生成通知の相対リンクが解決し、出力外参照なし。
- 配布補助`--check-legal`は未承認案を拒否。manifest.jsonへ改名したコピーも拒否。
- 本文・原文を含む一式に現在のworkspace/profile rootが埋め込まれていないことを検査。
- `build/windows-legal-draft.review.zip`へ文書だけを格納。別実装の.NET ZIP readerで337ファイルの全SHA256と件数を照合し成功。SHA256は`f744bd02ecb1dcd531560fd687eaf3cd03cbd22cc9d821e3ac59d779101d357f`。製品ZIPではなく、実行ファイルやDLL、ソースアーカイブ本体を含めない。

最初の検証はPowerShellの未引用`-D...exe`引数の解析でHELPER指定に失敗したため引用して実行。続いてCMake listの不均衡な角括弧によってリンク一覧が分割されない検査側不備を修正し、上記すべて成功した。文面・原文の検査は省略していない。

## レビューで確定すること

公開ページURL、製品の版・コミットと最終バイナリ対応、各条項の適用範囲、CRT保護条件／同意方法を確認する。公開先未設定を架空URLで埋めず、検証候補を公開済みとも表示しない。ユーザー確認前にreview_onlyを外さない。原文コピーだけで実組込みコードの全条件を認定しない。
