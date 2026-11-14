# キャッシュなし依存取得の検証結果

2026-11-14、DIST-03を完了しました。

## 確認した範囲

専用worktreeの`cmake/VerifyWindowsCleanDependencies.cmake`を、通常のMSVC x64環境から未存在の依存rootへ実行しました。既存のソース・ダウンロード・ビルド成果物はコピーせず、vcpkgのバイナリキャッシュも使っていません。

- 固定Gitソース17件を新規取得し、NinjaをMSVC/NMakeで内部構築。
- vcpkg本体・GNU・MSYS2・ビルド用ツール・Cyrus SASLを取得し、固定ハッシュを検証。
- KIMAP/KSMTPまで標準の全Release依存ステージを構築・インストール。
- GNUのRelease DLL監査と再配置試験に成功。
- 構築後も17件のGitソース原本にtracked変更なし。取得物一覧52ファイルのSHA512再照合もすべて一致。

全工程は約46分で終了コード0でした。詳細ログ・完了receiptの識別子は[検証記録](../../agent/dist03-clean-dependency-verification.md)に保管しています。[再実行手順](dependencies.md#キャッシュなしの取得構築検証)では必ず別の新rootを指定してください。

## 以前のTLSエラー

CMake status 35は、Safe Shell内のWindows Schannelが`SEC_E_NO_CREDENTIALS`で失敗する制約と切り分けました。通常環境では同じCMake・証明書検証ONで取得とハッシュ照合に成功しています。TLS検証の無効化や外部curlへの切替はしていません。

vcpkgの「Asset cache hit」はCMake取得スクリプトの成功にも表示されます。今回は空の専用領域から開始しており、過去のキャッシュを流用した結果ではありません。

## 別途必要な検証

既存Qt SDK・MSVC/Windows SDK・CMake・Git・Pythonは作業者準備対象です。これらの新規導入や、legal付き実ZIPの生成・最終監査・クリーンWindowsでの起動を完了したという記録ではありません。DIST-01/02は引き続き未完了です。
