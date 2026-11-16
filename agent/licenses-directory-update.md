# 配布通知ディレクトリの統一

- `096bb0e`と`spec/license-handling.md`に従い、最終ZIPの通知を`SmoothMail/licenses/`へ統一した。本体・DLLの`runtime/`、ランチャー、READMEの配置は維持する。
- 配布補助のライセンス収集先とZIP名・SHA256一覧の相対名を変更し、旧`license/`が残らないことをテストする。
- MSVC Releaseで補助を再構築し、`package_guards`成功。C++自己試験に加え、ZIPの独立展開・ハッシュ照合・配置検査を実施。既存製品ZIPや利用者の保存物は変更していない。
