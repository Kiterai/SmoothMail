# Microsoft Visual C++ランタイムに限る追加条件（レビュー案）

**本案は未承認です。公開前に配布担当者へ適用されるMicrosoft契約との整合と、利用者・再配布者の同意取得方法をレビューしてください。**

## 1. 適用対象

この追加条件は、SmoothMailとともに提供されるMicrosoft製ReleaseランタイムDLLだけに適用します。対象ファイルは`COMPONENTS.md`の`msvc-crt`欄で特定します。

© Microsoft Corporation. All rights reserved.

SmoothMailのMITコード、Qt、KDE、GNUその他の第三者OSSコードには、このMicrosoftコード向けの制限を適用しません。MicrosoftランタイムをMIT・LGPL・GPLで再ライセンスするものでもありません。

## 2. 使用

適法に提供されたMicrosoftランタイムを、SmoothMailおよびそれを使用するアプリケーションの実行に必要な範囲でインストール・使用できます。Microsoftおよびその供給者は、この文面で明示的に許可された範囲を除く権利を保持します。

法律上許容される場合、およびMicrosoftコードに含まれるオープンソース部分のライセンスが要求する場合を除き、Microsoftランタイムの技術的制限の回避、リバースエンジニアリング、逆コンパイル、逆アセンブル、ソースコード導出を行わないでください。Microsoftおよび供給者の通知を削除・隠蔽・改変しないでください。

この制限は、OSSライブラリの改変・差替え・再リンクや、その改変のデバッグに必要なSmoothMail／OSS部分の解析を禁止しません。強行法規による権利も制限しません。

## 3. 再配布

この案内はMicrosoftランタイム単体の無条件な再配布権を付与するものではありません。SmoothMailを再配布する場合、Microsoftコードは適法に許可されたアプリケーションの一部として、元の通知・本追加条件とともに未改変で提供してください。再配布者および外部のエンドユーザーには、Microsoftの適用契約で要求される保護条件への同意を求めてください。

独自にMicrosoftコードを取得・追加・変更して配布する場合は、自身に適用されるMicrosoft契約とREDIST対象を確認してください。Preview・Pre-release・Beta、Debugランタイム、再配布対象外の工具を製品に含めないでください。

Microsoftの名称・商標を、Microsoftが当該アプリケーションを作成・推奨・保証したかのように使用しないでください。Microsoftコードを、ソース開示や改変権の付与を要求する条件の対象に変更して配布しないでください。この条件は別のOSS部分の元のライセンスに基づく権利を取り消すものではありません。

## 4. 法令・保証・責任

Microsoftコードを違法な目的で使用せず、適用される国内・国際の輸出管理法令（仕向地、利用者、最終用途に関する規制を含む）を遵守してください。

Microsoftコードは現状有姿で提供されます。適用法で許される範囲で、Microsoftおよびその供給者は、明示の保証・保証条件を付与せず、商品性、特定目的適合性、権利非侵害についての黙示の保証を否認します。この配布によりMicrosoftがSmoothMailのサポートを約束するものではありません。

適用法で許される範囲で、Microsoftおよびその供給者に対して回復できる損害は直接損害の5米ドルを上限とし、結果的損害、逸失利益、特別損害、間接損害、付随的損害は対象外とします。損害発生の可能性を知らされていた場合も同様です。適用法がこれらの制限を認めない場合はその限りではなく、消費者の強行法上の権利、故意・重過失、人身損害等について法律上排除できない責任を排除しません。

この案内は、利用者にすでに適用されるMicrosoftとの契約を置き換えたり、Microsoftを代理して新しい権利を付与したりするものではありません。

## 5. 参照先

- Visual Studio 2022 REDIST一覧: https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution
- Visual Studio 2022 Community使用許諾（他エディションはその適用契約）: https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/
- Visual C++ 2015–2022 Runtimeの公式使用許諾: https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/
- Microsoft輸出管理: https://www.microsoft.com/exporting

単体ランタイムの使用許諾を、配布者の再配布権の根拠と混同しないでください。MSVC 2022の正当利用と再配布条件の遵守は配布作業担当者の責務です。
