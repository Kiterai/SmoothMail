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

- QtKeychain 0.15.0（ad7344c45a86a4f66cbafc4b081b5f7b876cb0b7）をBuildKeychainDependencies.cmakeでDebug構築済み（keychain-build.log）。Qt6Keychain::Qt6Keychain。COPYING/keychain.hをライセンス情報として配置。秘密情報jobにはsetInsecureFallback(false)必須。実OS資格情報のテストはguestのみで行う。
- Account/ServerSettingsは秘密情報を含まない値型。AccountRepositoryはQSQLITEを専用QThreadで作成/使用/破棄し、設定の検証/追加/再読込/並び順の原子的更新を提供。schema version>1は拒否、重複IDは上書きしない。CredentialStoreはQtKeychainの非同期read/write/remove、平文fallback=false、エラー文字列が空でも失敗扱い。ホストCTest9ターゲット成功（credentialstoreはopt-inなしでskip）。Windows SandboxでSMOOTHMAIL_TEST_KEYCHAIN=1の資格情報テスト4 passed（日本語/8192文字token/read/delete）とrepository5 passed。ログsandbox-mail-output/{credentialstore,accountrepository}-results.txt。ホストのOS資格情報は変更していない。
- アカウント追加画面をmainへ接続。SSL/TLS既定、秘密情報はPassword echo mode。AccountRegistrarは毎回新しいUUIDで秘密を2件保存してからDBへ追加し、途中失敗時は両秘密を削除する。OAuthはまだ認可未実装のためパスワード代用を拒否する。接続テスト/同期は未接続。mainがAppLocalDataLocationへ設定DBを開くが、UI単体テストは永続データを開かない。
- 追加画面後CTest10ターゲット成功。Sandboxでcredentialstore5 passed（50000文字の保存失敗後rollbackとDB非公開、正常追加も確認）。SQLite :memory: はファイルパスではないためmkpathしない。アカウント表示/保存エラーはプレインテキスト。OAuthの実認可にはGoogle/Microsoftアプリ登録とclient IDが必要で未提供。
- ConnectionTesterは専用QThreadでKIMAP/KSMTP接続テストを実行。KJob完了を最大30秒/中断監視、破棄時はworkerへinterruption。TLS証明書proxyは常にfalse。SMTPはNotAuthenticated状態とstrict proxyを根拠にTLS確立を判定して認証gateへ渡す。IMAPのTLS/STARTTLSはLoginJob内部の暗号化確立後CAPABILITY/認証方式検査に依存（API上pre-auth TLS単独jobがない）。平文IMAPは明示CAPABILITY後にpolicyで検査。job error+state+encryptionModeを成功条件とする。
- Sandboxのconnectiontester 11 passed。信頼済みSSL/TLS・STARTTLS成功、未信頼証明書で認証送信ゼロも確認。tests/dataの鍵/証明書はOpenSSL生成の公開テスト用で製品へ含めない。初回KI18nはctorのGUI threadでsetApplicationDomainし、worker初期化によるQObjectイベントフィルタ警告を防止。アカウント画面の接続テストを実接続層へ接続し、テスト/保存中の設定編集を禁止。
- core/threadingはアカウント内Message-ID照合、曖昧な重複ID拒否、In-Reply-To優先/References末尾fallback、循環/自己参照拒否と直線チェーン圧縮、返信ヘッダ生成、一意ID生成を提供。件名やReply-Toでは関連付けない。tst_threading7 passed（init/cleanup含む）。UIへのツリー反映は未接続。
- ConnectionTesterはMailServiceへ改名し、認証後のLIST/STATUS/EXAMINE/UID FETCHでフォルダと完全メールを取得。ヘッダサイズを先に取得してMIME上限を検査する。失敗時は不完全snapshotを公開しない。ApplicationControllerがアカウントごとの更新を直列化、資格情報を必要時にreadする。選択フォルダ/F5/30秒更新、未読集計（子含む）/ソート/スレッド/閲覧へ接続。更新失敗時に既存表示を消さず、別アカウント更新で閲覧を切替えない。本文は引き続きソースの安全なプレインテキスト表示。
- AccountRepositoryにfolder/messageキャッシュ追加。account/folder/UIDVALIDITY/UIDで保存し、snapshot単位のtransactionで旧UID・削除・フォルダ消失を反映。失敗はrollback。キャッシュMIME解析もDBworker内。起動時のreloadはGUIキューへ遅延させ、constructor直後のsignal購読前にloadedを失うraceを防止。
- 既存+拡張ホストCTest11ターゲット成功。Sandbox connectiontester12 passed（メール2件/STATUS未読/本文/Reply-To/UIDVALIDITY変更後1件の取得）。最初の全体実行ではsyncが一度timeout、単独とその後の全体再実行は成功。testの待機上限は実jobの30秒を越える45秒へ変更。継続監視。persistent cache/GUI表示/失敗時表示保持はQt Testで検証済み。送信・下書き・添付保存・履歴・OAuth・アカウントdrag orderは未実装。
- MessageComposerでUTF-8本文/件名、Date、一意Message-ID、返信ヘッダをKMimeで生成。transportSourceからBCCを除外し、storageSourceには下書き用BCCを保持。宛先空のdraftを許可し送信では拒否、制御文字/不正識別子のheader injection拒否。replyDraftはReply-To優先で全アドレスを初期化。tst_messagecomposer5 passed。UI/SMTP send/IMAP APPEND接続は次。
- ComposeDialog/SMTP SendJob/IMAP AppendJobを接続済み。senderは選択アカウント固定、失敗時は入力保持、送信自動再試行なし。\DraftsのLIST flag優先、存在確認済みDrafts名をfallbackに使い、特定できなければ保存拒否（INBOXへ代用しない）。Sandbox connectiontester13 passed（送信BCC envelopeのみ/下書きBCC保持/下書きfolderなし拒否）。ホストCTest12ターゲット成功。
- 前記sync timeoutの原因は完了signalのworker直接emitとm_busy解除のGUIqueued slotとのrace。QSignalSpy/即時observerが次の操作を開始するとbusyで落としていた。MailServiceの全完了通知を所有スレッドへ戻し、busy=falseを先に設定してからemitするよう修正。連続sync/send/draftテストで再現し修正後全成功。今後signal通知時には次操作を開始可能な状態を保証する。
- FolderModelはトップレベルアカウントだけを並び替える専用MIME drag/dropを実装。QTreeViewによる二重source削除を避けるためdrag protocolはCopyAction、model内で移動する。並び順をrepository transactionへ接続し、失敗時は通知して再読込。mainwindow testでroot順/子へのdrop拒否/行数維持を確認。編集メニューはComposeDialogの最後の編集可能な入力欄へundo/redoを転送し、readonly/送信中/閉じた画面では無効。ホストCTest12ターゲット成功。
- 添付カードからDownloadsへ保存しDB履歴に記録する処理をrepository workerに追加。MimeParserで添付partだけを検証してdecodedBody（message/rfc822はbodyAsMessage encodedContent）を抽出。保存名の制御/format文字・パス区切り・ADS・Windows予約device名を処理、NewOnlyで原子的に既存ファイルを非上書き。同名は連番。保存失敗は履歴なし、履歴失敗後は保存済みpathとエラーを報告。
- DownloadHistoryDialogはsubject/date/absolutePath/SI bytesと右クリックの元メール/保存先を実装。source jumpはaccount/folder/UIDVALIDITY/UID照合、見つからなければ別メールを開かず状態表示。添付保存/ファイル名逸脱/同名非上書き/バイナリ/履歴はaccountrepository test、カードpartId通知はmessageviewer testで検証。ホストCTest12ターゲット成功。Downloadsへの実ホスト書込みや実ファイラー起動はしていない（テストはbuild配下）。
- OAuthClientはGoogle/Microsoft固定endpoint/scope、PKCE S256、system乱数state、loopback callback(Host/HTTPmethod/path/state/code重複検査)、HTTPS token exchange/refresh、手動redirect、タイムアウト/応答上限/取消/invalid_grantエラーを実装。ネットワークは非同期Qt Network。Sandbox tst_oauthclient6 passed（FakeNetworkと実loopback、外部接続なし）。UI/secret保存/メールへ渡す接続は後続。公式設定と資料はdocs/dev/oauth.md。実アプリ登録/実サービス認可は未提供。
- OAuthをAccountDialog/AccountRegistrar/AuthenticationBroker/IMAP/SMTPへ接続済み。ServerSettingsのprovider/clientIdは非秘密メタデータ、Keychainはpasswordとoauthの別key。OAuth grant JSONを通常パスワードで代用不可。brokerはaccess tokenをメモリcacheし、refresh rotationをKeychainへ保存できてからaccessを返す。保存上限超過/失効/キー欠如時はtokenを返さずpassword fallbackもなし。Sandbox oauthclient7 passed（実使い捨てKeychain+FakeNetwork）、MS短縮scope表記とcallback交換も検証。
- OAuth再認可はアカウント行の右クリックから明示実行し、同じaccount/protocolのgrantを差し替える。失効で勝手にブラウザを起動しない。新規認可のclient IDはSMOOTHMAIL_GOOGLE_CLIENT_ID/MICROSOFT_CLIENT_ID、Google公開desktop secretは任意環境変数。アカウント画面にスクロールを導入。暫定追加UIはspec/provisional/oauth-controls.md、手順はdocs/dev/oauth.md。
- 最新Debugの全15テストEXEをQt SDK未導入のWindows Sandboxで実行し、合計142 passed、0 failed、0 skipped（init/cleanup含む）。all-exit-code.txt=0、各ログall-tst_*.txt。ホストはnetwork3ターゲットを除くCTest12成功（資格情報はopt-inなしskip）。実Google/Microsoft認可・実送受信・配布ライセンス/Release梱包は未検証。外部アプリ登録とユーザ自身の認可が必要。
- KSMTPにもローカルpatchを追加。SessionPrivate::startHandshakeでAUTH/SIZE/STARTTLS/DSN/SMTPUTF8をclearし、TLS前広告の残留を防止（RFC3207）。BuildMailDependenciesは独立ksmtpコピーからbuild/Debug/ksmtp-fresh-ehloへ構築。原本クリーンを維持。Sandbox postTlsCapabilitiesAreAuthoritativeでbeforeXOAUTH2/afterPLAINを広告、旧DLLはAUTH送信して2 passed/1 failed、修正DLLは認証送信なしで3 passed/0 failed。ログsmtp-{unpatched,patched}-results.txt、buildログsmtp-patched-build.log。ライセンスはLGPL-2.1-or-laterのまま通知/差分を配置。
- c89b1d1で変更済み依存receiptの生成・configure時検証を追加。固定commit/patch SHA256/DLL SHA256/QtCore SHA256/configurationをJSON dataとして照合し、取り違え・欠如をfail closed。Python6テスト成功。ローカル整合性検証であり署名・配布元真正性の保証ではない。
- Reply-Toの存在と解析されたアドレス一覧を区別するhasReplyToを追加。空/不正Reply-Toがある場合Fromへの暗黙fallbackを防止。原本rawから再parseするDB cacheにも反映される。回帰テスト成功。
- guest実GUIでアカウント追加dialogの下端が拡大表示時に隠れる問題を発見。availableGeometryに初期サイズを制限して修正、決定/Cancelと入力エラー/取消、空履歴、tray hide/double-click restoreを手動確認。最新全15EXEは145 passed/0 failed/0 skipped、host CTest12 targets成功（OS秘密情報はopt-inなしskip）、receipt Python6成功。現時点で実サービス認可・実送受信・Release配布/全ライセンス監査は未完了。
- 利用者がGoogleの開発用アカウントを提供。通常パスワードはファイル/ログへ保存も認証送信もしていない。Google Desktop OAuth client IDは環境変数未設定、実ブラウザ認可も未実施。Googleアカウント作成とOAuthアプリ登録は別。以後、パスワードを再掲/コードへ固定しない。
- tests/manual/google_readiness.pyを作成。認証・送信なしでGoogleのIMAP993/SMTP465/STARTTLS587のTLSとXOAUTH2広告だけを確認。Safe Shell内では10013、利用者のコマンド許可後に外側で3件成功（TLS1.3/XOAUTH2）。Python imaplibのcapabilitiesはstr/bytes両対応。実アカウント認証は未検証。
- cmake/StageWindowsRuntime.cmakeでDebug開発用の起動フォルダを作成。dumpbin再帰imports+明示Qt plugins/SASL3機構+選択MSVC Debug CRT/UCRT。windeployqt6.12がqt6keychaindをSDK moduleと誤認するため、解決されたQt SDK DLLだけを入力にする（SDK変更なし）。qt.conf/start-smoothmail.cmd/runtime-sha256.txtを生成。出力既存時とReleaseは拒否。Python guard3 tests成功。現在の完成フォルダbuild/launch-debug。途中試行のbuild/run-debug*は未完成/不採用であり起動用には使わない。
- 新しいSDKなしguestにlaunch-debugだけをコピーしWindowsだけのPATHで起動成功。ロードモジュールはguest C:\SmoothMailとOS配下、qsqliteも配置からロード。別フォルダにテストEXE+QtTestDLLだけ追加して、runtimeをlaunch-debugに限定した合成通信14 passed/0 failed/0 skipped。ログsandbox-mail-output/{launch-debug-loaded-modules.txt,runtime-probe-results.txt}。外部Google認証とRelease配布の検証ではない。詳細docs/dev/windows-launch.md。
- Google Desktop client ID提供後、opt-in smoothmail_google_probeを追加（SMOOTHMAIL_BUILD_LIVE_GOOGLE_PROBE=ON）。実OAuthClient→refresh→MailService::testを使い、メール読出し/送信/DB/資格情報ストア保存なし。Qt protocol logging/session/TLS鍵ログを無効化し、トークンを出力しない。ローカルbuild/run-google-oauth-probe.cmdに公開IDとlogin hintを設定、launch-debug/start-google-dev.cmdも追加（いずれもignored）。OAuth known error codeのみの安全な登録エラー診断を追加し、選択QtTest3 passed。host CTest12成功（最初のPATH=launch-debugのみの試行はoffscreen plugin不足で中断、SDK plugin pathを明示して再試験）。
- 利用者許可後にoutside実行2226767faa6f4c7db4fbb27c316ac113でブラウザ認可開始。利用者からテスター制限の403 access_deniedを報告。OAuth appを作成した同じCloud projectのGoogle Auth Platform/Audience/Test usersにログインするアカウントを追加する必要がある。認可コード/トークンは未取得、実IMAP/SMTP認証へ未到達。待機probeをそのexecution_idだけで停止（Chromeはgroupに含まれず停止していない）。設定後は新規認可flowで再試験。launch-debug/smoothmail.exeへ最新診断版を反映しruntime-sha256も更新済み。
- Test users追加後のoutside再試験c5ea16cfd07a4a999558319e433afbbdはGoogle callback受領後、token endpointのinvalid_requestで終了。実tokenは未取得。続いて明示許可されたgoogle_token_configuration.py（公開clientID+故意無効dummy code/verifierだけ、secretなし）でTOKEN_CONFIGURATION_ERROR=invalid_request / CLIENT_SECRET_REQUIRED=trueを確認（execution f222d1c9a8434b5b8a65e09bb7a739bd）。Google Desktop registrationのclient_secretが必要。ユーザにはJSONをrepo外へ保存しパスだけ共有してもらう。
- OAuthClientに既知の"client_secret is missing."だけを判定する固定メッセージを追加（生応答echoなし）。選択error QtTest3 passed。probeに--registration-fileを追加し、installed.client_id一致+非空client_secretを検証してメモリのみで使用、ファイル内容は出力しない。Web client JSONは拒否。設定JSON自体は未提供/未読込み。OAuth public native clientのsecretを機密保持の根拠にはせずPKCE/state/TLSを維持する。

- 次はKDE依存の構築・検証。指定Qt SDK以外のQtを取得・ビルドしない。第三者ソースとビルド成果物はGit対象外のbuild配下へ分離する。
- 本文のURLポリシー（http/httpsのみ、userinfo禁止）は暫定仕様。URLデコード後の制御文字を拒否し、リンククリック時も再検査する。
- 表示時にはQTextDocumentの正規化後の文字列で範囲計算する。CRLFとUTF-16サロゲートペアの回帰テストを維持する。
- MimeParserは同期の値変換アダプター。GUIではなくメールワーカーから呼ぶこと。現在は画面への読込み未接続。原文をバイト列で保持し、KMimeオブジェクトをUIへ渡さない。
- 本文と添付メタデータを分離。MIME上限値と抽出時チェックの限界はspec/provisional/mime-limits.mdに記載。
- MimeParser追加後のDebugビルドとCTest全3ターゲットが成功。詳細ログはmimeparser-results.txtで15 passed（init/cleanupを含む）。日本語3文字コードとMIME転送符号化・添付名・抽出上限を確認。
- CoreAddons/KConfig/KCrash 6.31.0のDebug DLLを同一Qt SDK/MSVCに対して構築済み。BuildFrameworkDependencies.cmakeの共通処理を含む再実行も成功（build/deps/framework-bootstrap.log）。
- KIMAP/KSMTPにはKIOCoreが必要。KIOCORE_ONLYでもKService/Solid/Crashのfind_packageが必須。KI18nはlibintl、SolidはFlex/Bisonが必要で、Windows向けの依存取得・固定手順を続けて整備する。
- GitHub/Release assets/ftp.gnu.org/repo.msys2.orgの接続許可を承認済み。Craft blueprintはbuild/deps/src/craft-blueprints-kdeに調査用取得済み（2eb0b809261d66717cd3060d02065f3b21e70802）。Qtを取得するCraft実行はしない。
- 重要: KSMTP 26.08.3 loginjob.cpp:155以降で優先認証方式からLOGIN/PLAINへのfallbackがある。OAuthでは利用可能機構を検査してからジョブ開始し、欠如時はトークンを一切送らないテストが必須。
- BuildSaslDependencies/BuildXOAuth2DependencyでSASL 2.1.28と3機構をDebug DLL構築済み。CyrusのGitソースには生成ファイルがないためSHA256固定の公式tarballを使用。上流nmakeは子ディレクトリで引数が継承されないため、lib/pluginsを個別実行する。
- SASL単体テストにはWindowsのWSAStartupが必要。Qt Network経由ではなく生APIを呼ぶテストなので明示初期化している。全4 CTestターゲットが成功。
- WinFlexBison 2.5.25（1176da2adaf972b2216b1ac8dd611e128fe80bc5）をMSVCでソースビルド済み。BuildWinFlexBison.cmakeによる再実行も成功。Solid 6.31.0のDebug DLL構築も成功（solid-build.log）。Solid以降の手順のスクリプト化は後続作業。
- vcpkg-recipesは177e8175121536d414f405ed8b3099b325a42638、ツールは2026-10-29を固定。gettext-libintl/libiconvのDebug/Release構築・インストールが成功（execution 1a7ecddffd914c019253840369040033、build/deps/vcpkg-native.log）。
- vcpkg組込みHTTP/WinHTTPはサンドボックスのproxyに失敗。X_VCPKG_ASSET_SOURCESでPython helper→Git付属curlを使用して取得できた。証明書検証を無効にしたりproxy変数を消したりしない。helperのGNU/MSYS2 canonical URL変換とSHA512等の4テストは成功。
- BuildNativeDependencies.cmakeによるgettext[tools]の構築・インストールは成功（execution d9506241a0024247876a29f847b430b4、build/deps/native-tools-build.log）。KI18nのconfigureはlibintlだけでなくGettextのmsgfmt/msgmergeも必須。
- 参考用に取得したlibintl-liteはAPI/翻訳機能がGNU gettextと異なり、Windows代替として採用していない。製品へ組み込まない。
- authenticationDecisionを追加。TLS/証明書未検証、必須機構欠如、不明なenumを拒否する。KSMTPと同じ厳密な機構名で判定するため、大文字小文字を緩く許可してライブラリ側のfallbackを誘発しない。LoginJob作成前・秘密情報設定前にこの判定を適用すること。現在はポリシー単体テスト段階で接続層へ未接続。
- SASLを有効にしたdevビルドでCTest全5ターゲットが成功（connectionpolicy/mainwindow/messageviewer/mimeparser/sasl）。
- Native bootstrapはVSINSTALLDIR末尾の区切りを削除してVCPKG_VISUAL_STUDIO_PATHへ渡す。末尾区切りがあるとvcpkgのVS照合が失敗した。
- KIO 6.31.0のfavicon除外パッチはユーザ承認済み。patches/kioとTHIRD_PARTY_NOTICES.mdに明記し、BuildKioDependencies.cmakeで独立コピーへ適用。原本・Qtは変更しない。残るライセンス監査は未完了で、配布物全体がMITであるとは記載しない。
- BuildI18nDependencies.cmakeでKI18n/KService 6.31.0のDebug DLL構築に成功（execution adc1844f7c284914a852c024c54d31a2、i18n-build.log）。native Debugのlibintlとホスト用gettextのパスを明示指定。上流ソースは変更していない。
- KDE_BASE/SASL両テスト有効でCTest全6ターゲットが成功。テスト/実行PATHにはnative/x64-windows/debug/bin、native/x64-windows/tools/gettext/bin、install/Debug/bin、指定Qt/binが必要。ホストツールは自身のRelease DLLをexe隣からロードする。
- dumpbin /dependentsでKF6I18n.dllがQt6Cored/intl-8等、KF6Service.dllがQt6Cored/Qt6Xmld/ConfigCore/CoreAddons等を使用することを確認。これらのテストにKIOはリンクしていない。
- パッチ適用KIO 6.31.0のDebug構築が成功（kio-build.log）。KIOのcompile_commands、ninja -t deps（kio-headers.log）、dumpbin /exports（kio-exports.log）にfaviconscache実装・ヘッダ・クラスエクスポートが含まれないことを確認。対象favicon関数は空文字を返す。原本のgit statusはクリーン。
- KIOテストを有効にしたCTest全7ターゲット成功。パッチコピー保護のPythonテスト5件も成功。開発prefixにはGPL等のworker/補助実行ファイルがあるので、製品へ一括コピーしない。
- BuildMailDependencies.cmakeでKIMAP/KSMTP 26.08.3（package 6.8.3）Debug構築成功（build/deps/mail-build.log）。SMOOTHMAIL_TEST_MAILは合成loopback向けの開発用テストで既定OFF。コンパイル成功だが通信は未検証。Safe Shell内では最小Python TCP接続もWinError 10013となり、localhost許可設定でも接続不可。既存7ターゲットは成功、mailtransportは当初90秒timeout。現在はinitでloopback確認し早期失敗する。サンドボックス外実行はしていない。
- .gitattributesで*.patchをLF固定。KIOパッチのSHA256によるコピーキー・インストール済み通知照合がcheckoutの改行変換で変わらないようにする。
- パッチ作業コピーの保護はcmake/PreparePatchedSource.cmakeへ共通化。PrepareKioSource.cmakeは互換wrapperで既存KIOのコピー名・receiptを維持。DependencyBuild.cmakeは任意の検証済みsource/binaryを構築するhelperを分離。Python保護テスト6件成功（別dependency名の独立コピーも検証）。
- 2026-11-13 Windows Sandboxでmailtransportを実行可能と確認。外部network Disableでもguest内loopbackは使える。build/sandbox-mail-testをRO、sandbox-mail-outputをRW共有。プロジェクト全体の共有は.agent-config denyのため不可。stageへQt/KDE/native/SASL/Debug CRT/UCRTをコピーし、guest C:\MailTestで実行。ログ回収後Sandboxはterminate成功。
- 同テストは再実行とも10 passed／1 failed（init/cleanup含む）。SMTP 3機構、IMAP PLAIN/LOGIN、OAuth広告不足拒否、両方STARTTLS不足拒否は成功。IMAP XOAUTH2はerror()==0だが未認証、errorTextはmalformed reply。testsの状態検証でerrorTextを出すよう改善。fixtureはSASL-IRを広告せず空continuationを返す。upstream loginjob.cppのCONTINUATION分岐はPLAIN以外の空応答をMALFORMEDとしてerrorTextだけ設定してemitResultする（error未設定）。上流パッチ未適用、SASL-IR対応サーバーでの比較も未実施。成功判定にerrorだけを使わないこと。詳細ログbuild/sandbox-mail-output/mailtransport-results.txt、exit-code.txt=1。
- 上記KIMAP問題はpatches/kimap/26.08.3-fix-sasl-continuation.patchで修正済み。BuildMailDependenciesは独立コピーで構築し、LICENSES/差分/変更通知をprefix/share/licenses/kimapへ配置。build/deps/src/kimapはクリーン。buildログmail-patched-build.log。
- 2026-11-13 拡張mailtransportをWindows Sandboxで比較実行。unpatched-results.txtは12 passed/3 failed、patched-results.txtは15 passed/0 failed。空challenge(+ /bare+/+ =)と認証拒否/不正応答を追加。未修正版で空challenge2つとmalformedのerror!=0が失敗、修正後は全成功。patched-exit-code=0、unpatched-exit-code=3。ホストの既存CTest7ターゲットも成功。ログ回収後Sandbox終了。TLS証明書検証/SASL-IR/実OAuth/本番接続への認証gate統合は次の課題。
