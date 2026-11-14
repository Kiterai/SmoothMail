# KIO 6.31.0の暫定パッチ

- 上流: https://invent.kde.org/frameworks/kio
- 対象コミット: `2dd4bdc916a6f46d297d8788f5b628181483315d`（v6.31.0）
- Windows向けの現行パッチ: `6.31.0-smoothmail-portable-core.patch`
- 元のfavicon単独差分（履歴・他OS向け）: `6.31.0-disable-favicon-cache.patch`
- 変更日: 2026-11-12（favicon）、2026-11-14（Windowsの探索パス）
- 変更者: SmoothMail contributors

メール処理に不要なfaviconキャッシュをビルドから除外します。利用者の承認を得た暫定措置であり、上流の正式修正ではありません。

## 変更と影響

- `src/core/CMakeLists.txt`: `faviconscache.cpp`をKF6KIOCoreから除外。
- `src/core/global.cpp`: 対応するprivateヘッダのincludeを除去。
- `KIO::favIconForUrl()`: 公開関数は維持し、すべてのURLに空文字列を返す。favicon検索は利用できません。
- GPL系の`faviconscache.cpp`／`faviconscache_p.h`を改変・別名転載・再ライセンスしません。
- 変更対象の`global.cpp`のLGPL-2.0-only表記と著作権表示は維持します。変更箇所にも日付・目的を明記します。

## 適用

`cmake/BuildKioDependencies.cmake`で、固定コミットの取得元から独立した作業コピーを作成し、`git apply --check`後に適用します。原本・Qt SDK・グローバルGit設定は変更しません。再実行時はコピーのHEAD・index・差分・追加ファイルを確認し、予期しない変更があれば上書きせず停止します。

SDKまたはパッチを変更する場合は別のビルドディレクトリを使います。対象バージョン変更時にパッチを無条件で流用しないでください。

## ライセンスと配布

詳細はリポジトリの`THIRD_PARTY_NOTICES.md`を参照してください。この除外だけでKIOや全依存の配布監査が完了したとは扱いません。開発用prefixの補助プログラムやworkerは製品への一括コピー対象ではありません。

## Windowsの絶対パス対策（2026-11-14）

### 経緯と方式

DIST-04の検査で、helper探索先とadmin workerの信頼判定に使う絶対ビルドprefixを検出しました。
通常の相対インストール先指定もECMが絶対化するため、設定だけでの除去はできません。
今回の利用者の承認に基づき、Windows向けの限定パッチとして対処します。
現行差分は既存のfavicon除外と今回の変更を一つにまとめ、固定原本から一回で適用します。
元の差分は履歴として残しますが、両方を重ねて適用してはいけません。

- `kioexec`: EXE隣の実行可能なhelperだけを解決します。見つからなければ空の結果とエラーを返し、Debug assertで落としたり、カレントディレクトリ/PATHの偽helperを新たに使用したりしません。
- 一般のExec解決: 既存のPATH検索は維持し、最後の絶対ビルドprefix候補をEXE隣へ置き換えます。
- `kdesu`: ビルドprefix候補をEXE隣へ置き換え、従来のPATHフォールバックは維持します。
- `kioworker`: KLibexecの実行時候補、Qtの配置先、既存のPATHフォールバックは維持し、ビルドprefixの追加だけを除きます。未配置なら従来のエラーを返します。
- `admin:`: Windowsではworkerを明示的に拒否します。信頼パスを空文字・相対パスへ変えて許可することはしません。通常のプロトコルメタデータ列挙は行われ得ますが、admin workerのインスタンス生成や外部worker起動には進みません。
- 絶対位置のマクロはWindowsコンパイル時に定義しません。Windows以外の探索とadmin信頼判定は元のままです。

### 失われる機能・保守

ビルド時のprefixにだけあるhelperの探索と、Windowsでのadmin worker利用を失います。
汎用KIOとの機能上の完全互換は保証しません。KIOCore全体やTLSエラー情報APIの無効化ではありません。
現在のSmoothMailは主にKSslErrorUiDataを使用し、URL起動にはQDesktopServicesを使います。
製品にはKIOの外部worker/helperを一括同梱しません。将来必要になった場合は、実行時の配置・探索・信頼性を設計し直します。

ビルド先をpatchハッシュ別にし、変更済みDLL・Qt・commit・patch・構成をreceiptで検証します。
旧favicon単独receiptはWindows向け現行ビルドとして受理しません。原本分離と変更拒否の保護は維持します。
上流更新時にはhelperの探索順とadminの信頼境界を再点検し、上流で適切な再配置/機能選択が実現したら撤去を検討します。
具体的な検証結果は`agent/kde-runtime-path-patches.md`を参照してください。

### 追加変更のライセンス

変更する`desktopexecparser.cpp`はLGPL-2.0-or-later、`worker.cpp`はLGPL-2.0-onlyです。
既存の`global.cpp`のLGPL-2.0-onlyも維持します。変更箇所に変更者・日付・目的を表示し、元の著作権/SPDXは削除しません。
KIOや差分をMITへ変更するものではありません。GPL系favicon実装のコードを転用する変更でもありません。
配布時には適用条件に従う完全な変更済みライブラリのソースと必要な構築/インストールスクリプト、
原文・著作権・非標準動作/制限・ソース提供方法の通知が必要です。パッチだけの公開で義務を済ませません。
利用者の改変・差替え・再リンク等の権利を維持し、製品legalへの対応付けと全体監査を別途行います。
