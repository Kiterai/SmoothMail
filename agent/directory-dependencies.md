# ソースディレクトリの循環依存解消

- `core/account.h`が通信層の接続方針ヘッダを取り込む原因だった認証・暗号化方式のenumを`core/connectionsettings.h`へ分離した。通信時の認証判定は`mail/connectionpolicy.*`に維持する。
- `MimeParser`とメッセージ生成を`src/mime/`、`OAuthClient`と認可データ変換を`src/oauth/`へ移動した。実装・公開型・処理内容は変更せず、includeとCMakeのソース一覧を更新した。旧パスに転送ヘッダは残さない。
- 保存層は共通モデル・MIME・OAuthだけを利用し、通信層には依存しない。通信層から保存層への参照は維持する。`smoothmail_storage`の接続方針へのリンクを削除し、実際の利用者である`smoothmail_transport`のprivate依存へ変更した。
- `tests/python/test_directory_dependencies.py`で直接includeの解決、全ソースディレクトリの循環不在、依存方向を検査する。CMake生成の`oauthdefaults.h`だけは生成ヘッダとして明示的に除外する。
- `docs/dev/architecture.md`と4種類のDOT/SVGを既存の生成ツールで更新した。図・表はソースからの自動生成であり、外部の図素材は使用していない。ツールは`build/architecture-tools/`へ隔離した。

## 検証

- 既存Release依存とQt 6.12.0/MSVCを使い、専用の`build/directory-refactor/`へ本体と通常テストを構築。CTest全14ターゲット成功。OS資格情報への書込み・意図的クラッシュ等のopt-inケースは従来どおりスキップ。
- 通信試験、OAuth試験、GUI smoke、Google手動probeも追加でコンパイル・リンク成功。これらの実行、実サービス接続、OS資格情報操作、GUI操作は行っていない。
- `PYTHONUTF8=1 python tools/generate_architecture.py --tools-root build/architecture-tools --test`でPython全52試験成功（新規の依存検査5件を含む）。初回は既存ライセンス試験の既定cp932読み込みが失敗したため、コード変更ではなくUTF-8環境を指定して再確認した。
- `python tools/generate_architecture.py --tools-root build/architecture-tools`で依存図・表を再生成し、ディレクトリ図にも循環がないことを確認。
