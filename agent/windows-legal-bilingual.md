# legal文面の英語正本・日本語訳への改訂

2026-11-15。利用者指定により、配布文面を英語正本と日本語訳へ再構成した。前回の文面・レビューZIPは保存し、未承認状態を維持する。

後続の仕様7a3514e対応により、現管理先は`licenses/common/`及び`licenses/editions/windows-x64/`となり、文面は`.env`から生成するテンプレートへ移行した。[現行方針・検証結果](license-template-policy.md)。以下は英日版初稿の履歴である。

## 整理方針

- `legal/windows/{README,NOTICE,SOURCE-AND-REPLACEMENT,CHANGES,MICROSOFT-RUNTIME}.md`を英語正本、それぞれの`.ja.md`を日本語訳とする。日本語訳は常体・漢語基調。第三者ライセンスそのものの正文指定を変更しない。
- 利用許諾を独自に言い換えて拡張することを避け、MIT、LGPL v2.1/v3、GPL v3、Microsoft条項、必須謝辞を原文に倣うか引用する。日本語の引用部分は参考訳であり、公式訳として表示しない。
- 開発用設定、実行試験、ハッシュ検査、SDK差異、作業経緯、ビルドコマンド、監査作業上の説明を受領者向け本文から除外。改変者・日付・変更内容、原文・著作権・謝辞、ソースアクセスと改変／差替え権は保持する。
- 配布者向けの根拠・未決事項は当文書と`docs/dev/distribution.md`へ残す。Microsoft再配布者／エンドユーザーの同意取得方法、契約と保護条件の整合、公開URL、最終構成は人のレビュー対象である。

## Microsoft原本

旧READMEから開発経緯を省いたため、原本情報はここへ移す。2026-11-15取得の公式掲載DOCXを使用し、再取得は行っていない。

- https://visualstudio.microsoft.com/wp-content/uploads/2021/11/Visual-Studio-2022-Community-License-EN.docx
  - SHA256 `41a207b10c8ab91d0d2f10a854715f73dca54509581692d2fe179aa3ffcb8540`
  - 第4節のDistributable Code／Distribution Requirementsを引用する。
- https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx
  - SHA256 `f1e3d56ceb2ad68aae0711b910375009e651ac5530fa0760f0dea6e81e54fae1`
  - Scope of License、Export Restrictions、免責／責任制限、消費者の権利の該当部分を引用する。

単体Runtime契約を再配布許可の根拠にせず、VS契約の再配布条項と区別する。引用は抜粋であり、全文・地域別条項・例外を置き換えない。CRT以外のOSSへ制限を拡張しない。

## 生成処理

- COMPONENTS／SOURCES及び19コンポーネント別通知も英語・日本語の対で生成する。
- 全52文書をmanifestへ直接登録。共通文書はSmoothMailの項目、各日本語通知は該当コンポーネントへ登録し、元ライセンス原文は保持する。現行のZIP収集／リソース埋込みはリンク先をたどらないため、明示登録で同梱漏れに対応した。ダイアログUIや同意取得機能は変更していない。
- 旧収集物の開発経緯入り`notices/`はコピーしない。Qt SBOMは任意入力の根拠JSONとして`evidence/`に保存し、開発説明入りMarkdownは生成しない。
- `licenses/`内の原文資料は未改変で保持する。第三者原文の改名・翻訳・正文指定の上書きは行わない。

## 検証

英語文面の引用14ブロックを、上記SHA256照合済みDOCXとローカルのMIT/LGPL/GPL/CMU/FreeType原文へ照合し成功。空白を正規化し、FreeTypeの指定形式の`<year>`のみ2026へ置換して比較した。GPL v3の引用を`offering`ではなく原文どおり`offer`に補正した。

`build/verify-legal-quotes.ps1`はこの照合の局所証跡。訳文の内容は英語と節・表・項目を照合して確認し、引用と同じ自動試験で翻訳の完全性まで認定したとは扱わない。

最終出力は`build/windows-legal-draft-en-ja`。`verify_windows_legal_draft.cmake`で34ファイル／19コンポーネントの対応、271原文のSHA256不変、52文書（26対）の言語指定・相対リンク・manifest登録、未承認／改名資料の拒否を確認し成功した。日本語訳を欠くコピー及び共通文書の登録を欠くコピーを拒否する負対照も成功した。

レビューZIPは`build/windows-legal-draft-en-ja.review.zip`。独立した.NET ZIP readerで328ファイル全てのSHA256と件数を照合した。SHA256は`b47678f9ff34539bf7094acf926a9b1e42f005e05c806a50be75adecb8a3d1a5`。workspace/profile root非混入検査も成功した。保存済みの旧レビューZIPは上書きしていない。製品ZIPの承認・公開、通常起動先の更新は行っていない。
