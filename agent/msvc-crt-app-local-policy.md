# MSVC CRTの必要DLL同梱方針と検証

2026-11-15。利用者の承認方針:

- 必要なMSVC CRT Release DLLを改変せずポータブルZIPへ同梱する。
- MSVC 2022の正当な利用・適用条件の遵守は配布作業担当者の責務。
- 自動検査で利用資格を認定せず、追加の資格承認待ちをブロッカーにしない。
- MicrosoftコードをMIT/LGPLへ再ライセンスしない。通知・適用条件の最終整理はDIST-02に残す。

## 実装

- `cmake/WindowsCrt.cmake`: VS 2022 x64 Release再配布フォルダ形式を検査。
- `DUMPBIN /DEPENDENTS`でEXE・解決済みDLL・指定Qt/SASLプラグインの通常/遅延importを調べ、CRT自身のimportも再帰的にたどる。System32/Qt側の同名CRTは採用しない。
- CRTフォルダ全体のglobコピーを廃止。未知のCRT import・不足・Debug・ディレクトリ外への解決を拒否する。
- Windows自身の`msvcp_win.dll`はVC再配布CRTではないため、OS依存として区別する。
- `tools/windows-crt`はMSVC/NMakeで内部構築するビルド専用C++補助。製品へ同梱しない。
- CRTのx64 PE32+、vcruntime140基準とのFileVersion一致、Microsoft Authenticode署名、コピー前後のバイト一致/SHA256を確認する。windeployqt後も再確認する。
- 署名の許可publisherはMicrosoft Corporation、Microsoft Windows、Microsoft Windows Software Compatibility Publisher（組織Microsoft Corporation）。信頼チェーンはWinVerifyTrustで検証し、ネットワーク/対話は無効。オンライン失効情報の鮮度確認を完了したという意味ではない。
- legalレビューのCRT部品対応を、固定10件から実際の同梱候補の既知CRT一覧に変更。未承認review_onlyを維持する。

## 実ステージ

既存の配布用Release候補`build/merge-check-release/smoothmail.exe`を使い、SDK・通常起動runtime・保存済み候補を上書きせず新規収集した。今回、最新ソースからの配布用EXE再構築や最終legal付き製品ZIPは行っていない。

`build/crt-policy-runtime-final`は34ファイル。CRTは従来10件から次の5件へ減少:

- msvcp140.dll
- msvcp140_1.dll
- msvcp140_2.dll
- vcruntime140.dll
- vcruntime140_1.dll

全5件のFileVersionは14.44.35211.0。署名・x64・版・原本一致を確認し、34ファイル全体が既存の供給元情報限定パス検査を通過した。
`build/legal-crt-policy-final-review`で34ファイル/19コンポーネントの対応と、未承認資料の配布入力拒否を確認。

## 回帰・隔離起動

- 配布補助CTestは6対象すべて成功。
- import解析の隣接行・大文字・遅延import、プラグイン起点・推移依存・不要DLL非選択、欠落・Debug・未知・不正rootの拒否を合成試験で確認。
- 原本CRTを変更せず、自前fixtureで改変コピー、不正/x86 PE、署名除去、版混在の拒否を確認。
- 検証専用ZIPを作り、新規・ネットワーク無効のWindows Sandboxで空白/日本語パスへ展開。全34ファイルのハッシュ一致、開発用PATHなしのGUI起動、5 CRTが展開先から実ロードされたことを確認。
- 初回起動試験はウィンドウ生成前の確認が早すぎたため条件待ちへ修正。次の判定ではOSのmsvcp_winをVC CRTと誤分類したため、対象をv14再配布CRTへ限定した。実ロード先を確認した最終試験は成功。
- guestは新規で実アカウントなし。ネットワーク・資格情報・実メールは使用しない。起動したSandboxは出力回収後に終了済み。

## 証跡と限界

- `build/crt-policy-stage-final.log`
- `build/crt-policy-tools-tests.log`
- `build/crt-policy-privacy.log`
- `build/crt-policy-legal.log`
- `build/crt-policy-smoke-input/runtime.zip`: legal未収集の検証専用ZIP、製品配布物ではない
- `build/crt-policy-smoke-output/result-final.json`: 起動・34ハッシュ・5 CRT実ロード確認

通常/遅延importと指定プラグインの確認であり、任意の動的LoadLibraryや実通信を含む全機能試験を代替しない。最終通知/同意条件・対応ソース/差替え・legal付き最終ZIP/クリーン環境検証は、DIST-01/02の残作業として維持する。
