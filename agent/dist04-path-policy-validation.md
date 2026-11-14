# DIST-04の検査側対応

2026-11-14。Qt/SASLの外部由来パス保持を許容する利用者方針に基づく。

- `cmake/runtime-path-policy.json`へレビュー済み11 DLL・15範囲を登録。
- 全体SHA256・範囲SHA256・offset/length・由来と用途を固定。供給元profile本文は保存しない。
- 明示root/ユーザー名/PC名は元バイト全体で検査し、承認範囲でも免除しない。
- 許容範囲は検査用メモリ上だけでmaskし、残りは従来どおり検査。配布DLLは変更しない。
- `PackageWindows.cmake`から固定ポリシーを使い、ZIP書込み時にも再確認。
- ポリシー不一致・追加パス・別名・範囲内の作業者識別子・重複/不正ポリシーを合成試験で拒否。拒否時ZIP非公開も確認。
- 既存のポリシーなしCLIは厳格検査を維持。

結果:

- package/build/package-entryの3 CTest対象すべて成功。
- 統合Release収集物39ファイルは`--audit-stage-with-policy`で通過。
- 入力rootは実workspace、元作業者profile、プロセスprofile/Temp/ユーザー名/PC名を指定。具体値は報告へ記載しない。
- 全体・範囲hashは前回監査のSDK原本一致/署名ValidとSASL上流固定値の根拠へ結び付けた。

ログ: `build/dist04-package-build.log`、`build/dist04-package-tests.log`、`build/dist04-runtime-audit.log`。
ポリシーは信頼されたレビュー入力。供給物が更新されたら自動追認せず、差分と由来を再レビューする。
DIST-04のruntimeパス検査は完了。legal付き実ZIPの監査とクリーン環境検証はDIST-01/02へ残す。
