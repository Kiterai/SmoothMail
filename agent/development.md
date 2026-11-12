# 実装の引継ぎ

- 独自コードはC++20。既存Qt SDKをQt6_ROOT/CMAKE_PREFIX_PATHで指定し、別Qtを取得しない。
- 最初の実装はアプリ起動・空のメイン画面・トレイ常駐。通信等の未実装操作は無効化し、テスト用メールを通常画面へ混入させない。
- トレイを利用できない環境ではcloseを受理して操作不能な常駐を避ける。このフォールバックは暫定仕様を参照。
- アイコンはmainwindow.cppのQPainter描画による独自の封筒図形。外部素材なし。
- Qtテストはoffscreenプラットフォームで実行。実デスクトップでのトレイ動作は別途確認が必要。
- 次の大きな課題は、指定Qt SDKに対するKIMAP/KSMTP/KMimeとCyrus SASL/XOAUTH2のビルド・ライセンス・配布検証。未完了。

## 検証記録

- Qt 6.12.0 msvc2022_64、MSVC 19.44でDebugビルドとmainwindowのQt Test（4ケース）が成功。
- サンドボックス内でcmdシェルを使用し、vcvars64.batをcallしてからQt付属のCMake/Ninjaを使用。Git Bashからcmd /cを渡すと引数変換でコマンドが実行されない場合があるため避ける。
- CTest実行時はQt SDKのbinをPATHへ追加する。GUIテストはoffscreenであり、実トレイ操作の保証にはならない。
- 本文ビューを含むDebugビルドとCTestの全2ターゲットが成功。ACL付与エラーはユーザによる追加readルールの削除後に解消。
- テストの詳細ログはQt Testの`-o build/dev/name-results.txt,txt`で取得できる。Safe Shellのstdoutが空でもログを確認可能。mainwindowは6 passed、messageviewerは23 passed（init/cleanupを含む）。offscreenでフォント警告があるためピクセル外観の保証はしない。
- `cmake/BuildMimeDependencies.cmake`でECM/KCodecs/KMime 6.31.0のDebug DLLを指定Qt SDKに対してビルド・ローカルインストール済み。ソースとprefixはbuild/deps配下。KIMAP/KSMTP 26.08.3はソース確認のみ、KIO/CoreAddons/I18n/SASL構築は未完了。

## 次の作業

- 次はKDE依存の構築・検証。指定Qt SDK以外のQtを取得・ビルドしない。第三者ソースとビルド成果物はGit対象外のbuild配下へ分離する。
- 本文のURLポリシー（http/httpsのみ、userinfo禁止）は暫定仕様。URLデコード後の制御文字を拒否し、リンククリック時も再検査する。
- 表示時にはQTextDocumentの正規化後の文字列で範囲計算する。CRLFとUTF-16サロゲートペアの回帰テストを維持する。
