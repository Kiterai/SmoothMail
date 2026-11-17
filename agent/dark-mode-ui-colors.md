# ダークモードの文字・アイコン色

- MessageViewerの件名・日時・差出人・宛先はRichTextの黒指定を除き、QLabelのWindowTextを継承する。表示済みメールもパレット変更へ追従する。太字、HTMLエスケープ、空欄の灰色、非ASCIIアドレスの赤色警告は維持する。
- 一覧の非重要の星・既読/未読の丸・添付クリップは描画時のパレットを参照する。通常はText、選択中はHighlightedTextとし、有効・無効・アクティブ・非アクティブのグループを区別する。重要の星の黄色は維持する。
- ページ前後ボタンは標準の固定矢印アイコンから、ButtonTextで生成する独自の線画へ変更する。PaletteChange/StyleChange/ActivationChangeで再生成し、Disabledの画像を明示的に登録する。外部素材は使用しない。
- `build/directory-refactor`で本体・tst_mainwindow・tst_messageviewerのビルド成功。CTestのmainwindow/messageviewerは2対象成功。パレットを注入するoffscreenテストであり、Windows実デスクトップのダークモード外観は未確認。通常起動用・配布用runtimeの更新は行っていない。
- 回帰テストは明暗×選択×有効×アクティブの16通りで一覧アイコンを描画し、前後矢印のパレット変更と無効色、メール情報の通常文字に固定前景色がないことを検査する。
