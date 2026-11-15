# 仕様7a3514eのライセンス文書管理方針への対応

2026-11-15。利用者が確定した`spec/distribution.md`の共通／版別管理、テンプレート化、`.env`による3項目の指定、ZIP内`licenses/`配置へ対応した。利用者の仕様コミット自体は変更していない。

## 管理対象

- `licenses/common/`: README、MIT通知、対応ソース／差替え、コンポーネント表、ソース表、コンポーネント通知の英語正本／日本語訳テンプレート。
- `licenses/editions/windows-x64/`: 当版の第三者ソフトウェア・Qt/CMU謝辞・Windows差替え情報、改変通知、Microsoftランタイム条件の対訳テンプレート。
- ソフトウェア名は`@SOFTWARE_NAME@`、配布URLは`@DISTRIBUTION_URL@`、ビルド版は`@BUILD_VERSION@`。版名は`-DEDITION=windows-x64`で指定する。表の行・原文参照等は検証済み入力から生成する。
- `.env.example`をGit管理し、`.env`は全階層でignoreする。今回、実際の`.env`の作成・変更・読取りはしていない。明示的な`.env.example`入力でプレビューを作成した。
- 固有の著作権者名、ライセンス原文、実バイナリ名、原本アーカイブの識別子はソフトウェア名称の変更で書き換えない。

## 生成と配布

`cmake/LicenseTemplates.cmake`は専用`.env`をデータとして解釈する。UTF-8 KEY=valueと外側引用符だけを扱い、シェル実行・変数展開・行末コメントは扱わない。必須値・未知キー・重複・危険文字・認証情報付き又は非HTTPS URL・未知テンプレート変数を拒否し、設定値はエラーへ出力しない。既存のアプリ資格情報用.envを兼用しない運用を手順へ明記した。

`PrepareWindowsLegalDraft.cmake`は共通と版別のテンプレートから、版・ビルドに応じた新規出力を生成する。版／設定の変更後は別出力で再生成・再レビューする。生成は承認ではなく、`review_only`を維持し、配布用manifestは作らない。配布時に承認済みの文面を無断再生成しない。

旧ツールの`LEGAL_DIR`／`SMOOTHMAIL_LEGAL_DIR`という引数名は互換性のため維持する。ZIP内の配置先は`SmoothMail/licenses/`、原文は`originals/`へ統一した。旧`legal/`は新ZIPへ書き込まない。rootの`LICENSE`は元のMIT原文として保持する。ソース側の開発経緯`THIRD_PARTY_NOTICES.md`は製品ZIPへ追加せず、生成済み資料が指定されたダイアログにも重複して埋め込まない。開発ビルドの未設定時フォールバックは残す。

## 検証結果

- `.env`／テンプレートの14ケース成功。引用符・必須値・空値・未知／重複キー・不正URL・展開記法・不正文字・未定義変数等を検査し、診断への値の流出も確認した。
- `.env.example`から`build/windows-licenses-policy-review`へ生成成功。34ファイル／19コンポーネント、原文271件の全SHA256一致、54文書（27対）の英日対応・常体・相対リンク・manifest登録・未展開変数の不存在・未承認／改名入力拒否を確認。
- 別の合成`.env`で名称・URL・ビルド版の3値を変更し、英語／日本語の生成結果に反映すること、MIT原文の著作権が不変であることを確認。
- 現在のテンプレート中の英語引用14ブロックを原文へ再照合し成功（既存のMicrosoft DOCX SHA照合を維持）。第三者原文は翻訳・テンプレート化しない。
- 独立prefixで配布補助を再構築し、CTest 8対象が全成功。合成ZIPをCMake/libarchiveで独立展開して全ハッシュ照合し、新`licenses/`配置・旧`legal/`非混入を確認。`build/licenses-policy-tools-final.log`。
- Qt Coreリソースの独立ビルド／実ロードで21エントリを確認し、設定済みビルド版・URL・原文参照が組み込まれ、旧開発経緯の表示項目がないことを確認。`build/licenses-policy-resource.log`。製品アプリ全体や通常起動先の更新は行っていない。
- `.env`がGit ignoreされることを確認し、`.env.example`は追跡対象とする。プレビューに`.env`とworkspace/profile rootが混入しないことも検査した。

レビューZIPは`build/windows-licenses-policy-review.review.zip`。文書だけを`licenses/`以下へ格納し、独立した.NET ZIP readerで330ファイル全てのSHA256と件数を照合した。ZIPのSHA256は`9fd04990d7b42e78843df22973a0e96b293ecdc349fe16e31c9da3437c1dce77`。実行ファイル・DLL・資格情報・ソースアーカイブ本体は含めない。

## 残る確認

今回のプレビューURLは`.env.example`の`example.invalid`であり公開先ではない。実配布用の設定、製品バイナリ／コミットとソースの対応、通知・条件の人のレビュー、CRT条件への同意方法、最終manifest／製品ZIP／公開先の確認は従来どおりDIST-01/02に残る。MSVC利用資格の追加承認待ちを新設したものではない。旧レビュー成果物・Qt SDK・通常起動先・利用者作業は変更していない。
