# KCrash/KIOのWindows実行時パス対策

2026-11-14。利用者承認に基づく実装。base `195b7c5`、分岐`fix/kcrash-kio-private-paths`。

## 隔離

- 専用worktreeで作業。元作業の未コミットnative関連変更は取り込まず、元のソース・prefix・起動中アプリを変更していない。
- 原本KCrash/KIOは固定commitのローカル独立clone。テスト用prefixは既存成果物を読み取って専用領域に複製し、そこだけを上書き。
- worktreeが深くKIOの生成ヘッダ相対includeでC1083が発生したため、専用の短いTemp依存rootで再構築。失敗した自分の生成CMakeCacheだけを削除して試した短縮aliasでは解決せず、実際に短いディレクトリを使用。
- Git 2.36の所有者検査にはbuild内の一時global設定ファイルをプロセス限定で使用。利用者のglobal設定は書き換えていない。worktreeのbranch/commit用共有.git書込みとTemp出力の権限は個別に承認済み。

## 実装

- KCrash: Windowsだけ最後の絶対libexec候補を除去。環境変数・EXE隣・Qt実行時libexecを維持。公開header/ABIは変更しない。
- KIO: 既存favicon除外と今回の差分を`6.31.0-smoothmail-portable-core.patch`へまとめる。元単独差分は履歴として保持し、二重適用しない。
- WindowsのkioexecはEXE隣でのみ解決し、未配置ならエラー。一般Execとkdesuの元prefix候補はEXE隣に置換（既存PATH探索は維持）。
- kioworkerはKLibexecの実行時候補・Qt・既存PATHを維持し、元prefix候補だけを除く。
- Windowsのadmin protocolはWorker::createWorkerの最初で拒否。空文字startsWithで信頼判定を無効化しない。非Windowsの元の比較は残す。
- 生成ヘッダの絶対定義自体もWindowsプリプロセスで除外する。非Windowsの元のパス定義と探索を維持。
- KCrashを第7の変更済み依存としてreceipt対象へ追加。アプリはWindowsでKF6Crashを明示find_packageする（共有KIOのprivate依存は消費側へimportされないため）。
- KIO binaryディレクトリはpatchハッシュ別。BuildMailの承認済みpatch照合と合成receipt fixtureも統合差分へ更新。

## 検証結果

- 両patchの`git apply --check`成功。原本は未変更。独立patched-srcの差分保護・固定commitを使用。
- KCrash/KIOのMSVC Debug/Release構築・専用prefixへのinstall成功。
- 追加のportablekde試験はDebugで成功。Releaseアプリの全対象ビルド成功、ホストで実行可能な18 CTest対象が全成功。
- portablekdeではDLLのロード実体SHA256をconfigure時と照合し、KIOの拒否文言を確認してから挙動を試す。
- 合成の実行可能ファイルを使って一般Exec、同梱kioexecの解決を確認。ファイルを起動する試験ではない。
- kioexecがEXE隣にない場合、カレントディレクトリ/PATHの偽helperを解決せずエラーになることを確認。
- DrKonqiはEXE隣の無害なfixtureで探索だけを確認し、クラッシュやhelper起動をしない。KCrash handler操作はテストプロセス内だけ。
- 合成adminプラグインのメタデータが列挙される状態を作り、公開storedGet(admin:)が指定エラーで終了し、プラグインconstructorの専用markerが作られないことを確認。
- ビルトインdata:プロトコルは正常取得、KSslErrorUiDataの構築/コピーとfavicon除外も成功。実サービス/資格情報/実メールへ接続していない。
- 既存のパッチ・receipt・download・stagingガードも成功。receiptの不足KCrash/旧favicon単独KIOを拒否するケースを追加（統合receipt 5ケース）。
- 独立Releaseステージを新規収集。既存C++配布補助でKCrash/KIOCoreが実際のworkspace・Temp依存root・元の開発root・Qt rootを含む検査に通過。
- 収集物全体の個別監査の拒否は15件→13件。残りはQt10件、GNU2件、SASL1件（base時点の資材を使用）。検査規則や署名Qtは変更していない。

## 残る事項

- Linux/macOSの実構築は未検証。プリプロセスで既存動作を残すが、Windows以外の実試験成功とは主張しない。
- 外部kioworker/DrKonqi/kdesuの実起動・クラッシュ回復や、汎用デスクトップ用途の完全互換は検証対象外。失われる元prefix探索とWindows admin機能をREADME/第三者通知へ明記。
- 元作業で進行中のnativeプロフィール変更とは未統合。そちらの成果物での再検証が必要。
- legal・対応ソース提供、署名SDKのメタデータ方針、全体privacy検査・実ZIP・クリーン環境検証は未完了。
- 元worktreeへmerge/cherry-pickせず、独立分岐にコミットして引き渡す。
