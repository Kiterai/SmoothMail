# SmoothMail Windows版 legal案

**レビュー用・未承認（2026-11-15）。このディレクトリは承認済みの配布入力ではありません。**

利用者向けの同梱文面は次の4ファイルです。原文ライセンス・著作権表示と併せて使用します。

1. [利用許諾・第三者通知](NOTICE.md)
2. [対応ソースとライブラリ差替え](SOURCE-AND-REPLACEMENT.md)
3. [変更済みライブラリの通知](CHANGES.md)
4. [Microsoft CRTに限る追加条件案](MICROSOFT-RUNTIME.md)

これらの文面は最新の通常構成の検証候補を基準にしています。改変exportを追加した試験専用DLL、QtTest、テストEXE、ホスト工具は製品の同梱対象ではありません。MITの`LICENSE`や第三者の原文を置き換えるものではありません。

## 一式をレビューする

`cmake/PrepareWindowsLegalDraft.cmake`で、既存の原文収集結果から文面・19コンポーネントの対応表・ソース一覧を新しいフォルダへまとめます。例：

```bat
cmake -DEVIDENCE=build/d05-legal-baseline-review -DSOURCE_REVIEW=build/d05-source-review -DQT_SBOM_REVIEW=build/qt-shipped-sbom-review.json -DOUTPUT=build/windows-legal-draft -P cmake/PrepareWindowsLegalDraft.cmake
```

出力の`README.md`を起点に確認してください。`manifest.review.json`は`review_only: true`を維持し、`manifest.json`は生成しません。承認後の製品生成や公開は、この作業に含みません。

生成物の`COMPONENTS.md`は34同梱ファイルの対応表、`SOURCES.md`は23ソースアーカイブの一覧、`notices/`はコンポーネント別の原文リンクです。`licenses/`は収集済み原文を未改変で保持します。`QT-ATTRIBUTIONS.review.md`はSDK依存63件の著作権・条件の補助一覧です。`evidence/`内の旧レビュー文面やSBOMは根拠資料であり、利用者向け文面の代わりではありません。

## 確認をお願いする点

- 文面の用語、適用範囲、原文・著作権・謝辞の表示。
- Microsoft CRTだけに限定した追加条件、配布者に適用される契約と条件の整合、および利用者・再配布者の同意取得方法。文面の配置だけで同意を取得したとは扱いません。
- 公開ページURL、製品版／コミット、最終バイナリとソース一式の対応。公開ページが決まるまではソース提供済みとは表示しません。
- Qt内蔵第三者コード・KDE等のファイル単位の条件。原文集合に含まれる全ライセンスが製品全体に適用されるわけではありません。既存の利用者確認を踏まえた案ですが、最終構成の確認を置き換えません。
- Installation Informationが必要になる提供形態の場合の追加情報。現案は一般のWindows向けポータブルZIPを対象とし、特定装置への組込み配布を対象にしていません。

## Microsoft原文確認の根拠

2026-11-15に以下の公式掲載資料を確認しました。これらは配布担当者向けの参照であり、開発環境全体の契約をSmoothMail利用者へ一括適用しません。

- [Visual Studio 2022 Community License](https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/)のDistributable Code（第4節）とScope of License。
  - 掲載原本`Visual-Studio-2022-Community-License-EN.docx`のSHA256: `41a207b10c8ab91d0d2f10a854715f73dca54509581692d2fe179aa3ffcb8540`
  - 再配布コードへ主要な機能を追加すること、再配布者・外部利用者に同等以上の保護条件への同意を求めること等を確認。
- [Microsoft Visual C++ 2015–2022 Runtime](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/)。
  - 掲載原本`Visual-C-Runtime-2015-2022-License-1.docx`のSHA256: `f1e3d56ceb2ad68aae0711b910375009e651ac5530fa0760f0dea6e81e54fae1`
  - この単体使用許諾自体を第三者への再配布許可の根拠にしない。再配布権は配布者へ適用されるVisual Studio契約とREDIST一覧による。
- [Visual Studio 2022 REDIST一覧](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)。

有料版を一律に必要とする案でも、新たな利用資格承認を要求する案でもありません。MSVC 2022の正当利用と適用条件の遵守は、従来方針どおり配布作業担当者の責務です。
