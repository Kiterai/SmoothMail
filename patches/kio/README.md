# KIO 6.31.0の暫定パッチ

- 上流: https://invent.kde.org/frameworks/kio
- 対象コミット: `2dd4bdc916a6f46d297d8788f5b628181483315d`（v6.31.0）
- パッチ: `6.31.0-disable-favicon-cache.patch`
- 変更日: 2026-11-12
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
