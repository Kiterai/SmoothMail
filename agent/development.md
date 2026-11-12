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
