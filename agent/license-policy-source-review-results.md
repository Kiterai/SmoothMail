# ライセンス方針追記後の処理

2026-11-14。利用者仕様`71ea2a1`に対応。Qtオープンソース使用・同一配布ページでのソース別配布・情報ダイアログを反映した。

## 完了した準備・検証

- QtモジュールはSDK SBOMの代替条件からLGPL-3.0-onlyを選択。FreeTypeの代替はFTLを選び、原文と謝辞を収集。
- code.qt.ioのネットワーク許可後、対話的認証を使わないGit取得で、qtbase/qtsvgの固定commit checkoutを成功させた。SDKへはインストール/変更しない。
- 最終取得先`build/deps/qt-license-sources`。中断した初期取得先は別に保存し、他作業を消していない。
- `build/legal-windows-complete-evidence-review`: 39 runtimeファイル/19コンポーネント、309資料。Qt Base/Qt SVGのlicense_filesは168/7。LicenseFile参照の解決、FTL謝辞、review_onlyの配布入力拒否を確認。
- `build/source-windows-policy-final-review`: 21アーカイブ、未取得Qt0。初回ソーススナップショットは`8f08c2b`。全アーカイブを別領域へ展開しSHA256照合、7件の有効KDEパッチの`git apply --check`成功。全再構築・差替え完了ではない。
- `smoothmail_qt_sbom_audit`: 同梱10 Qtモジュール/プラグインからDEPENDS_ONをたどり、63パッケージ・未解決参照0件。生の供給元SBOMを転載せず、必要な識別子・条件・著作権・入力hashを`build/qt-shipped-sbom-review.json`へ記録。
- 合成SBOMで非同梱ツールの除外、循環、外部文書参照、未解決依存の失敗、既存出力非上書きを確認。
- 最終資料を埋め込んだDebug22対象・独立Releaseレビュー12対象・配布補助4対象すべて成功。
- 版/commit・プレインテキスト表示・メニュー起動・項目切替・原文/変更通知・レビュー未完了表示を試験。

## 残る確認

- 配布責任者による、Visual Studio/CRTの利用資格・契約遵守。公式VS 2022 REDISTは有効なライセンス保有者に未改変VC/redist内の配布を条件付き許可するが、個別の利用資格を自動認定しない。
- 技術監査: 実コンパイル/リンク対象、ヘッダ/生成物/内蔵第三者コードと条件の最終照合。Qt依存のWrapAtomic NOASSERTION等も未監査のまま成功認定しない。
- 提供ソースからの再構築、改変DLLの差替え、最終EXEのcommitと実ソース集合の一致。
- 最終manifest/通知の確定、同じ公開ページでのバイナリ・ソース取得、実ZIP/クリーン環境確認。

Qtの商用/OSS選択とソース提供方式は承認待ちではなく、仕様で解決済み。公開ページURLが未定でもローカル準備は進められるが、公開前に実到達性を確認する。
公開用manifest.jsonや製品ZIPは作成していない。通常起動runtime・既存Qt・資格情報・実メールには触れていない。

ログ:
`build/qt-legal-source-fetch-final.log`、
`build/source-windows-policy-final-review.log`、`build/source-review-verification.log`、
`build/license-policy-complete-{debug,release}-tests.log`、
`build/license-policy-tools-tests.log`。
