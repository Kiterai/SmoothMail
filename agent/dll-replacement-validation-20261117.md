# 最終Release ZIPのDLL差替え・SDK未導入環境の検証

## 結論

- 対象は[ビルド単純化](build-workflow-simplification.md)で生成したRelease ZIP。本体を再リンクせず、対応ソースから構築した改変QtSvg／KCodecsへの差替えに成功した。
- 新規・外部ネットワーク無効・SDK未導入のWindows Sandboxで、通常版と差替え版の本体がともに起動した。空白入りの別ディレクトリへ展開し、WindowsだけのPATH、`C:\Windows`を作業ディレクトリとして起動した。
- 各構成で372ファイルのハッシュが一致し、本体がロードした20依存DLLがすべて当該配置内にあることを確認した。差替え版の本体は新しいKCodecs DLLを実際にロードした。
- APIプローブがUnicode／XML／SVG描画／テキスト／SQLite／RFC2047を検査し、改変版のQtSvgとKCodecsそれぞれの追加export関数を実呼出しできた。通常版に改変識別を要求すると、期待どおり`Modified DLL marker missing`で失敗する負例も成功した。
- 8試験EXEは各構成120ケース成功、0失敗、0スキップ。通常版と差替え版の失敗差分はない。同じ検証を報告形式整理後にも再実行し、同じ結果を確認した。
- SDK未導入OSでの今回の本体実行・改変DLL差替えというBATCH-01の残件を確認できたため、利用者の指示に従い技術検証を完了とする。法的監査・実サービス試験・公開承認を実施したという意味ではない。

## 対象と変更範囲

- 元ZIP：`build/workflow-clean/build/rel/artifacts/SmoothMail-windows-x64.zip`。
- ZIP SHA256：`b336b36c79a5877e35deef5312b6e167e370721b602695eb3e30129defa31d88`。検証後も不変。
- 本体／提供ソースcommit：`352926821c9c573745cdc3501912546433f43365`。
- 同一EXE SHA256：`2b364c8f354743fb37b526bdfe9f51d062385d0462d7256bf609ab9d6ebf2a97`。両構成と元ZIPで一致。
- 提供ソースのqtsvg／kcodecsアーカイブを、固定SHA256・全ファイルtree digestで検証して別ディレクトリへ展開。KCodecsには元の配布と同じctypeパッチを適用した。
- QtSvgの原本644ファイル、KCodecsのパッチ適用後258ファイルを照合し、各ライブラリの変更が追加export関数1ファイルと`target_sources`の追記1箇所だけであることを確認した。既存APIやABIは変更していない。
- `smoothmail_source_replacement_svg`／`smoothmail_source_replacement_codecs`はともに45654を返す。以前の差替えプローブを再利用し、今回の改変DLLから実際に取得・実行した。
- 同じ元ZIPから作った検証用コピーの差分は`runtime/Qt6Svg.dll`と`runtime/KF6Codecs.dll`の2ファイルのみ。検証EXE8個、プローブ、Qt Test DLL、offscreenプラグインは両構成へ同じものを追加した。元の361ファイルにテスト専用11ファイルを加え、各372ファイルとして照合した。
- 元ZIP・既存Qt SDK・通常起動環境・実`.env`・ホストのアカウント／DB／資格情報は変更していない。実メール・外部通信・実アカウント認可は使用していない。

| DLL | 元のSHA256 | 改変版SHA256 |
|---|---|---|
| Qt6Svg.dll | `cce84059f1857ff1859db7c4fe10334e603d9ca81eb4d5f0a22ca0ac515b340c` | `c2dced3f13b314ccfe4ebca5a6e19cedd4f3f3323b72413f0fc47ec37d4b373a` |
| KF6Codecs.dll | `f4d16e35978485841987019aeec35076baa11f4b42ae197422d6c0861cb8311f` | `5a7706b5b0d4f759d9e3a72852b6feb9afd5686d138e1bda7fda27c0f964b9f6` |

## 試験結果

| 試験EXE | 通常版成功 | 差替え版成功 |
|---|---:|---:|
| tst_aboutdialog | 6 | 6 |
| tst_mimeregression | 12 | 12 |
| tst_messagecomposer | 6 | 6 |
| tst_mimeparser | 15 | 15 |
| tst_messageviewer | 30 | 30 |
| tst_accountrepository | 28 | 28 |
| tst_connectionpolicy | 16 | 16 |
| tst_sasl | 7 | 7 |
| 合計（init／cleanupを含む） | 120 | 120 |

## 再現と証跡

- 構築入口：`tests/source-replacement/BuildReview.cmake`。新しい出力先だけを許可し、既存SDK以外の独立prefixへインストールする。
- ソース改変照合：`tests/source-replacement/verify-modified-source.ps1`。
- 元ZIP保護・2 DLLのみの差分確認・試験セット生成：`tests/source-replacement/prepare_package_review.py`。
- guest実行：`tests/source-replacement/run-package-guest.ps1`。読取り専用の専用入力と、試験ログ専用の書込み出力だけを共有する。
- 構築先：`build/workflow-replacement2/`。構築ログ：`build/workflow-replacement-build2.log`。初回の試験用構築スクリプトは複数prefix引数の分割でQtを発見できず停止したが、引用付きの直接構成呼出しへ修正し、別の新規rootで構築成功した。配布物や元SDKの問題ではない。
- 初回guest試験も全件成功したが、Windows PowerShellの拡張文字列が試験名JSONへ余分なメタデータを出力したため、報告を通常の文字列へ修正して再実行した。
- 準備記録：`build/workflow-replacement2/guest/preparation.json`。
- 改変照合：`build/workflow-replacement2/source-modifications.json`。
- 最終guest結果：`build/workflow-replacement2/guest/output/confirmed/result.json`。`success=true`、`sdkAbsent=true`、両構成とも`appReady=true`／`probePassed=true`。
- 試験ログ：同ディレクトリの`baseline/`・`replacement/`。ホスト側でも各ログの成功件数・失敗／skipなし、同一本体、ロードされたKCodecsのハッシュと構築出力の一致を独立照合した。
- 出力回収後、使用したSandbox `479b5fbe-4d7d-49ff-870d-c0d0ab42996e`を正常に終了した。
- 追加した試験セット準備の回帰試験を含むPython47件は成功（任意の文書生成依存による8件skip）。これらのskipと、guestの120ケース／構成でskipがないことは区別する。

| 証跡 | SHA256 |
|---|---|
| 最終result.json | `551df05d266d3aa57b4b3e697e923ce373780e6129468e80050eed4b1e16dd56` |
| source-modifications.json | `3dd4d4a927157237e165ee9f8c82ab96b995eb544ca282434ef9f10d046afd9e` |
| preparation.json | `0eae13684febf78d26f0a91cce2cd868b7df4da494491414ef526c915bfe33cb` |

改変版DLLはこの技術検証専用であり、製品として公開しない。任意のABI非互換版や全依存の再構築を保証する試験ではない。製品を公開する際は、引き続き`docs/dev/distribution.md`の配布担当者の監査・公開条件を満たす必要がある。
