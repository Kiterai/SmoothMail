# KConfig 6.31.0：外部設定移行の明示的な無効化

- 上流: https://invent.kde.org/frameworks/kconfig
- 対象commit: `a25acdfb1f51c9d8af3426f03380a9de5c88817a`（v6.31.0）
- 差分: `6.31.0-optional-check-update.patch`
- 変更日・変更者: 2026-11-14、SmoothMail contributors
- 状態: 利用者の承認によるSmoothMail向け限定パッチ。上流の正式機能ではありません。

## 経緯と採用理由

Release配布検査で、KConfigCore DLLにビルド先の絶対パスを検出しました（DIST-04）。
上流は`KConfig::checkUpdate()`から呼ぶ`kconf_update.exe`の絶対インストール先をコンパイル定義に埋め込みます。
通常のインストール先指定・full変数の上書きでは再配置可能にならず、`/pathmap`も通常の文字列定数には効きません。

SmoothMail本体の設定はQSettings、DB移行は独自処理です。本体と現在取得済みの依存のC++ソースに、
`KConfig::checkUpdate()`を呼ぶ利用箇所は確認されませんでした。KConfig全体はKIO/KServiceのために必要です。

静的KConfigと未使用コード除去では最小EXEの検査を通せましたが、複数DLLへの組込みによる共有状態の重複と
再リンク条件等の検討範囲が広がります。helperの再配置対応は、不要な移行ツール・スクリプトの同梱と安全な探索まで
対象が広がるため、現時点では未使用機能だけを無効化する方針を選びました。

詳細な調査記録: `agent/dist04-build-settings-investigation.md`、`agent/dist04-patch-assessment.md`。

## 変更内容と機能制限

- `KCONFIG_USE_KCONF_UPDATE`オプションを追加。既定は`ON`で上流の処理を維持します。
- SmoothMailのWindows Debug/Releaseでは、明示的に`OFF`として動的ライブラリを構築します。
- OFF時も`KConfig::checkUpdate(const QString &, const QString &)`の宣言・export・ABIを維持します。
- OFF時の呼出しは固定文言の警告だけを出します。外部プロセスを起動せず、設定の変更・再読込・移行済み記録も行いません。
  引数や設定内容は警告へ出しません。**設定移行機能は失われるため、上流と機能上完全互換ではありません。**
- OFF時は不要な`KCONF_UPDATE_INSTALL_LOCATION`を定義しません。バイナリ文字列の事後書換えや配布検査の緩和は行いません。
- helper自体のビルド・インストールは変更しません。開発prefixに残るhelperや過去の成果物を製品へ一括コピーしてはいけません。
  製品収集は必要なDLL/プラグインに限定し、helperは同梱しません。
- 他OSの標準bootstrapにはこの無効化を適用しません。

## 適用・検証・保守

`cmake/BuildKconfigDependencies.cmake`が固定commitの原本から独立コピーを作り、`git apply --check`後に適用します。
元のQt SDK・上流原本・既存runtimeは変更しません。変更コピーに予期しない差分・追加・staged変更があれば停止します。
ビルド先にはpatchハッシュを含め、変更済みDLL・Qt・commit・構成・patchをreceiptで対応付けます。
`ValidateMailDependencies.cmake`はWindowsでそのreceiptも検証します。

検証対象は、オプションON/OFFの構築、OFF時の警告と設定非変更・非再読込、公開export、
KConfig/KServiceの通常利用、既存メール回帰、実Releaseランタイムの個人パス検査です。
実際の結果は`agent/build-tool-migration.md`とDIST-04の状態を参照してください。
このパッチだけで他のDLL・ライセンス・配布全体の監査を完了したとは扱いません。

上流更新時は単に差分が適用できるだけで流用せず、呼出し元・設定移行の要否を再確認します。
将来必要になった場合は、安全な再配置対応を設計するか無効化を見直します。
上流が適切な機能選択/再配置対応を提供した場合は、互換性と配布検査を確認してパッチを撤去します。

## ライセンスと配布上の義務

直接変更する`src/core/kconfig.cpp`は`LGPL-2.0-or-later`です。元のSPDX・著作権表示を維持し、
変更した各ファイルには変更者・日付・目的を記載しています。KConfigやその変更をMITへ再ライセンスするものではなく、
SmoothMail独自コードのMITと第三者コードの許諾条件を区別します。

配布時は適用される条件に従って、ライセンス原文・著作権・非標準ビルドであることと機能制限の通知に加え、
**配布DLLに対応する変更済みライブラリの完全なソースと必要なビルド/インストールスクリプト**を提供できるよう整備します。
パッチだけや上流の可変URLだけの公開を、義務の充足として扱いません。
固定原本・差分・構築手順を保存し、対応ソースアーカイブを取得できる形にするのが実務上分かりやすい方法です。

ライブラリ差替え・改変・再リンク等の利用者の権利を、適用される条件に反して制限しません。
receiptは出荷ビルドの取り違え防止用であり、利用者の改変DLLの実行を禁止する仕組みにはしません。
DLL方式だけで全義務が完了したとも扱いません。

ビルドはprefixの`share/licenses/kconfig`へ上流LICENSES、差分、本README、receiptを配置します。
製品の`legal/manifest.json`には実際のKConfig DLLと対応する原文・著作権・変更通知・ソース提供方法を割り当てます。
開発用receiptは製品通知として同梱しません。ビルド用ファイル・生成物を含む全体の許諾条件と配布方法の最終確認はDIST-02で行います。
