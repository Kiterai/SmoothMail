# Releaseランタイム検査停止の原因調査

調査日: 2026-11-14。KConfig限定パッチ適用後の`build/kconfig-patch-release-runtime`を読み取り専用で調査。
製品コード・検査規則・既存Qt SDK・DLLを変更していない。実アプリ/サービスを起動せず、資格情報・実メールは参照していない。

## 対象と手法

- 対象: 39ファイル（37 DLL、1 EXE、qt.conf）。従来のファイル単位検査で15 DLLが拒否。
- `tools/windows-package/package.cpp:206-224`と同じQt正規表現・Latin-1/UTF-8/両alignment UTF-16・明示root照合を使い、拒否箇所を列挙。
- PEセクションとPE32+ Debug Directory/CodeView RSDSを解析し、PDB参照か通常の実行時文字列かを区別した。
- 検出箇所はすべて`.rdata`の文字列。今回の15件について、命令バイトが偶然パスに見えただけという証拠はない。
- 供給元QtとステージコピーのSHA256一致、および両方のAuthenticode状態をPowerShellで比較。
- 調査用ツール・伏字済みJSONは`build/runtime-audit-investigation/`。個人/供給元のprofile部分は出力しない。
- KConfigCore/GuiとアプリEXEは現行検査を通過。法的監査や任意の個人情報の完全非混入を保証するものではない。

## 分類

| ファイル | 原因 | 評価 |
|---|---|---|
| KF6Crash.dll | `<BUILD_ROOT>/build/deps/install/Release/bin` | 今回のビルド先が実際に残る |
| KF6KIOCore.dll | 上記bin、kioexec、kdesu、lib/plugins/kf6/kio | 今回のビルド先が実際に残る |
| iconv-2.dll | CodeViewの`.../buildtrees/libiconv/.../iconv-2.pdb` | 今回のビルド先が実際に残る |
| intl-8.dll | locale/lib/INSTALLPREFIXとCodeViewのintl-8.pdb | 今回のビルド先が実際に残る。MSYS形式のprefixも含む |
| Qtの10 DLL/プラグイン | 供給元profile下の絶対PDBパス。Widgetsは追加の.cppパス2件 | SDKに元々含まれる情報。今回のSmoothMailビルドが挿入したものではない |
| libsasl.dll | 上流の固定既定値`C:\CMU\bin\sasl2` | 個別のビルド者情報ではない。個人パスとしての過剰検出 |

QtCoreは、供給元PDBのほかに固定値`C:\temp\qt-common`と`C:\temp\qt-user`でも拒否される。
公開上流Qtの`qsettings.cpp`にも同じ既定フォールバック値が存在する。SDK対応ソース一式はローカルにないため、
参照した公開devソースとSDK全体が同一commitであるとは主張しない。
https://github.com/qt/qtbase/blob/dev/src/corelib/io/qsettings.cpp

## KCrash/KIOの対応箇所

- KCrash `src/config-kcrash.h.cmake:5`が`KDE_INSTALL_FULL_LIBEXECDIR`を定義。
  `src/kcrash.cpp:192-200`はクラッシュ報告ツールdrkonqi等の探索先へ追加する。
  設定で実行時に無効化しても、DLL内の文字列は消えない。
- KIO `src/core/config-kiocore.h.cmake:10-12`がlibexecとpluginの絶対位置を定義。
  `desktopexecparser.cpp:276-282,349,545`はkioexec/実行ファイル/kdesu探索へ利用。
  `worker.cpp:507-515`はkioworker探索へ利用。
- **KIOのpluginパスは単なる探索先ではない。** `worker.cpp:456-464`ではadmin workerの信頼境界として比較する。
  全パスを相対化/空文字化して検査を通すだけでは、この検証を壊すおそれがある。削除・再配置・機能無効化を設計する場合は別途安全性と回帰試験が必要。
- これらは`__FILE__`ではなく生成ヘッダの値なので、現在の`/pathmap`では解消しない。

## GNU依存

- iconv-2.dllの検出箇所はCodeView RSDSのPDBパスそのもの。raw offset `1078788`。
- intl-8.dllはPDB（offset `81696`）に加え、locale（`66672`）、lib（`80752`）、
  MSYS形式のインストールprefixを保持する。
- vcpkgレシピは`--enable-relocatable`を指定しているが、GNUの再配置処理は元の`INSTALLPREFIX`/`INSTALLDIR`を保持して
  現在のprefixを計算する実装である。「再配置可能」は「元の配置先文字列を含まない」という意味ではない。
- 根拠: gettext-runtime/intl/Makefile.inのLOCALEDIR、gnulib-lib/Makefile.inのINSTALLDIR、
  gnulib-lib/relocatable.c:497-502のINSTALLPREFIX/INSTALLDIR参照、dcigettext.c:301の既定locale。
- `DependencyBuild.cmake`のprivacy_optionsはプロジェクトのCMake依存構築へ渡されるが、
  `BuildNativeDependencies.cmake`はvcpkg installを独立実行し、GNU portはautoconf/makeで構築する。
  したがって親CMakeの`/PDBALTPATH`指定だけではこの経路をカバーしていない。
- PDB参照の正規化と、元prefixを含まない/個人に依存しない配置設計は別問題。再配置性を壊さず、port/triplet/overlay側まで対応する必要がある。

## Qt SDKに元々ある情報と署名

次の10ファイル全てで、ステージと供給元SDKのSHA256が一致し、双方のAuthenticodeは`Valid`だった。

| ファイル | CodeViewのPDB文字列offset | 追加の検出 |
|---|---:|---|
| Qt6Core.dll | 9339332 | 固定temp既定値2件 |
| Qt6Gui.dll | 7914836 | なし |
| Qt6Network.dll | 1362960 | なし |
| Qt6Sql.dll | 218712 | なし |
| Qt6Svg.dll | 435068 | なし |
| Qt6Widgets.dll | 5140620 | qdatetimeedit.cpp（4734912）、qabstractspinbox.cpp（4794592） |
| Qt6Xml.dll | 101280 | なし |
| platforms/qwindows.dll | 772400 | なし |
| sqldrivers/qsqlite.dll | 1784148 | なし |
| tls/qschannelbackend.dll | 185700 | なし |

PDBファイルを同梱しないだけでは、DLLのCodeViewにある参照文字列は除去されない。
プロジェクトのコンパイラ設定で既存Qt DLLのこの情報を変えることもできない。
なおSASLのCodeViewは既に相対名`smoothmail-sasl.pdb`で、今回の拒否原因ではない。

QtのPDB参照・ソースパスは署名対象のDLL内容に含まれる。
事後に書き換え/除去すれば供給元の署名をそのまま維持することはできず、PDBだけ処理してもWidgetsの.cppパスが残る。
署名付き供給物を無条件に書換える対処や、Qtという名前だけで検査を除外する対処は採用していない。

## 検査側の問題と次の対応順

現行の`privatePath`は絶対パスの形だけで拒否し、次を区別しない:

1. 今回の作業者・PCのパス
2. 供給元のビルド情報
3. 公開上流に固定で書かれた既定値

固定値2種（QtCoreのtemp、SASLのCMU）はビルド者個人情報としては過剰検出。
将来認める場合も、公開原本で確認した**一致する値と用途**に限定して試験すべきであり、DLL名や全絶対パスを一括除外しない。
これだけでは本物のビルドパスやQtの供給元情報の問題は解決しない。

提案（未実装）:

- 自前構築分: KCrash/KIOの用途別対処、GNUのPDBと配置prefixをそれぞれ検証。KIOのadmin worker信頼判定は維持。
- 検査: 固定既定値を個人パスと誤分類する部分を、根拠・テスト付きで精密化。
- Qt: 「今回の作業者情報を含めない」条件と「供給元の既存署名DLLのメタデータも含めない」条件を明確にする。
  後者を要求するなら、現SDKをそのまま同梱する経路では満たせない。署名/由来/ABIを維持できる供給物または明示的に承認した処理の検討が必要。
- 承認済みSDKの供給元情報を許容する場合も、署名・ハッシュ・由来を確認した限定的な扱いを設計する。
  署名があることだけで個人パスが存在しないとは判断しない。

この調査では配布規則を変更せず、39ファイルのランタイムからZIPを公開していない。legal監査・クリーンWindows検証も未完了。
