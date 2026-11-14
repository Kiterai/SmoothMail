# 対応ソース・再構築・ライブラリ差替え

> レビュー用案。公開ページURLと製品版／コミットは公開前に確定します。現在のソース一覧は技術検証候補に対応し、公開済みとの表示ではありません。

## 対応ソースの入手

SmoothMailのWindowsバイナリZIPと同じ配布ページで、その版に対応するソース一式を別途ダウンロードできる形で提供します。バイナリだけを先行公開したり、将来のソース提供の約束・未履行の書面オファーで代替したりしません。

**配布ページ：公開前に確定。** 製品版・SmoothMailコミット・各アーカイブ名・SHA256は、同ページの対応表とソース一覧に記載します。上流のURLだけを対応ソースの提供とするものではありません。

ソース一式には、SmoothMail、対象Qt Base/Qt SVG、対象KDEライブラリ、QtKeychain、Cyrus SASL、XOAUTH2プラグイン、GNUライブラリの固定原本、適用パッチ、必要なビルド設定・スクリプト・生成入力を含めます。構築用のSolid、WinFlexBison、ECM、Ninja、固定vcpkgレシピも含め、構築専用ソースと製品同梱ライブラリを区別します。

Microsoft CRTはこのOSSソース一式の対象ではなく、Microsoftの条件で提供する未改変バイナリです。MSVCコンパイラ、Windows SDK、その他の外部工具はそれぞれの正規の入手方法で準備してください。

## 再構築

詳しい要件と手順はソース内の`docs/dev/build.md`、`docs/dev/distribution.md`、`cmake/`以下にあります。

1. 対応するソースアーカイブのSHA256を照合し、SmoothMailを新しいソースフォルダへ展開します。
2. Windows x64のMSVC 2022開発環境、互換性のあるQt SDK、CMake、Git等の必要工具を準備します。依存ライブラリのソースを偽のGitリポジトリにする必要はありません。
3. 依存構築先とアプリ構築先には新しいディレクトリを指定します。Release構築の例：

```bat
cmake -DQt6_ROOT=<Qt-SDK> -DCMAKE_BUILD_TYPE=Release -DDEPENDENCY_ROOT=<new-deps> -DAPPLICATION_BUILD=<new-app> -DSMOOTHMAIL_SOURCE_ARCHIVES=<archives> -DSMOOTHMAIL_SOURCE_COMMIT=<matching-commit> -P cmake/BuildWindows.cmake
```

提供された原本はアーカイブ／展開treeのハッシュで検証し、SmoothMailの既存パッチは別作業コピーに適用します。利用者自身の変更を試すときは原本を残し、別のソース／ビルド先で変更を適用してください。配布監査用の固定ハッシュへ改変物を無理に通す必要はありません。

Qt自体を構築する場合も、既存SDKを上書きしない独立prefixを使用します。`cmake/BuildQtSourceReview.cmake`はRelease／Schannel／内蔵SQLite／OpenSSL無効のABI検証用例です。これは配布SDKと全構築設定やバイト列が一致するという主張ではありません。最終配布物に合わせた構築設定は、その版のソース案内で確認してください。

## 改変DLLの利用

同梱ライブラリはDLLとして動的リンクしています。元のZIPと利用者データを退避し、SmoothMailを終了してから、コピーしたアプリフォルダ内で対応するDLL／プラグインを互換性のある改変版へ差し替えてください。

- x64、MSVC ABI、ライブラリの公開ABI、Release構成、Qtのメジャー版・互換性を合わせます。必要に応じて関連DLLとプラグインも一緒に再構築します。
- Qtのプラグインは`platforms/`、`sqldrivers/`、`tls/`、SASLの機構は`sasl2/`という配置を保ちます。
- 開発ツールのPATHに依存せず、コピー先の`smoothmail.exe`を直接起動して確認します。非互換な変更の場合は対応ソースからSmoothMailを再構築する方法も利用できます。
- 検証用の追加export・markerは差替え試験のためのもので、ライセンス上の権利行使に必要な条件ではありません。

配布生成時の原本検査・署名確認・ハッシュ固定を理由に、利用者が適用ライセンスに従って行う改変版の使用を禁止しません。改変版にSmoothMail配布者の署名・保証・動作確認があると表示しないでください。

一般のWindows向けポータブルZIPには、利用者が独自のDLLをロードするためのSmoothMail専用鍵・認可サーバは必要ありません。特定装置への組込み等、別の提供形態に変更する場合は、LGPL v3/GPL v3が要求するInstallation Informationの要否と必要情報を改めて確認・提供します。
