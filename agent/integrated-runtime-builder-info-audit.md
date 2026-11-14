# 統合Releaseランタイムの作業者情報検査

2026-11-14。対象commit `764a613`（GNU対策とKCrash/KIO限定パッチを統合済み）。
利用者方針: Qt/SASLの外部配布物・上流由来のパスは除去不要。今回のビルド作業者情報は残さない。

## 対象・手順

- `build/merge-check-release`を再buildし、`build/integrated-privacy-runtime`へ新規収集した。
- 入力は統合Release EXE、現行Release依存prefix、`native/gnu-runtime-v1`、既存Qt SDK、MSVC再配布CRT。
- 39ファイル（37 DLL、1 EXE、qt.conf）全体を検査。legalや実ZIPは今回の対象ではない。
- 既存の列挙診断で絶対パスの正規表現と明示rootを照合。Latin-1/UTF-8、両alignmentのUTF-16LEを使用。
- 追加照合ではworkspace、ビルド者profile/ユーザー名、検査プロセスのprofile/Temp/ユーザー名、PC名を検査。
  スラッシュ/バックスラッシュ、エスケープ形、MSYS drive形を含め、Latin-1/UTF-8と両alignmentのUTF-16LE/BEで検索。
  具体的な個人名やPC名、環境変数値は結果へ出力していない。
- 全38バイナリのSHA256を指定した入力元と照合。QtKeychainはQt SDKではなく自前依存として扱った。
- Qt10ファイルはSDK原本とのSHA256一致とステージコピーのAuthenticode `Valid`を確認。
- SASLの検出文字列は上流固定値`C:\CMU\bin\sasl2`そのもの。
  原本`cyrus-sasl/win32/include/config.h`のPLUGINDIR/CONFIGDIRと一致する。

## 結果

| 分類 | 結果 |
|---|---|
| 今回の作業者情報の明示照合 | 0件 |
| アプリ本体、KCrash、KIOCore、GNU、その他自前依存 | 絶対パス/明示root検出なし |
| Qt10ファイル | 供給元PDB/ソース参照、QtCoreの固定temp既定値。SDKと同じバイト列 |
| libsasl.dll | 上流固定ディレクトリのみ。PDB参照は相対名 |
| それ以外のファイル | 検出なし |

従来の厳格な検査で検出されるファイルは11件（Qt10 + SASL1）。
利用者が許容した由来の情報以外に、今回のビルド作業者情報の残存は見つからなかった。
DLL名で検査を一括除外した結果ではなく、全ファイルを検査して由来を分類した結果である。

## 証跡・範囲

- `build/integrated-privacy-build.log`
- `build/integrated-privacy-stage.log`
- `build/integrated-privacy-findings.json`: 伏字済み検出箇所・PE位置
- `build/integrated-privacy-verification.json`: 全ファイルのSHA256・入力元一致・Qt署名・作業者情報照合結果
- 検査用wrapper: `build/verify-integrated-runtime.cmake`、`build/verify-integrated-privacy.ps1`
- 列挙診断ソース: `build/runtime-audit-investigation/findings.cpp`

製品コード・自動配布検査の規則・SDK・通常起動runtimeは変更していない。
現行のPackageWindowsは供給元由来も拒否するため、この方針に合わせた由来判定の実装は別途必要。
legal監査・ZIP生成・クリーン環境確認の完了を意味しない。
検査はパス・既知の作業者識別情報を対象とし、任意の個人情報の完全非混入を数学的に保証するものではない。
実メール・資格情報・登録JSONは参照せず、実アプリ/認証操作も行っていない。
