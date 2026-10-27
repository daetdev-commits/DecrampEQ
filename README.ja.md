# DecrampEQ

[English](README.md) · [APIとスレッドの契約](docs/api.md) · [MITライセンス](LICENSE)

アナログ原型の**振幅カーブ**に高域まで近づける C++17 のヘッダーライブラリです。JUCE は不要です。対応カーブはベル、ロー／ハイシェルフ、ロー／ハイパス、ノッチ、チルト、バンドパス。ローパスとハイパスの傾きは 12～96 dB/oct を連続指定できます。

[48 kHzでのアナログ原型・修正版・旧版の比較](docs/corrected_curves.png)

## 使い方

`include/DecrampEQ.hpp` を取り込みます。CMake では `add_subdirectory()` したうえで `DecrampEQ::DecrampEQ` にリンクできます。

インストール後は `find_package(DecrampEQ 0.2 CONFIG REQUIRED)` でも利用できます。必要に応じて利用側のCMakeに `CMAKE_PREFIX_PATH` を指定してください。公開版候補は0.2.0で、APIは1.0未満の開発段階です。

```cpp
#include <DecrampEQ.hpp>

constexpr double fs = 48000.;
decramp::Band band{decramp::Type::lowPass, 3000., 0.70710678};
band.slopeDbPerOct = 42.;
band.fitTopHz = 20000.;

// 係数設計はワーカースレッドで行う。
auto coefficients = decramp::design(fs, band);

// 制御スレッド → 音声スレッド。updates と eq はメンバーとして保持する。
decramp::DesignQueue<8> updates;
if (coefficients.valid && coefficients.maxErrorDb < .1)
    updates.push(coefficients); // キュー満杯なら false。

// 音声スレッドではブロック先頭で最新の更新を取り出す。
decramp::SmoothProcessor<2, float> eq(480);
decramp::Design received, latest;
bool hasUpdate = false;
while (updates.pop(received)) {
    latest = received;
    hasUpdate = true;
}
if (hasUpdate) eq.set(latest);
eq.process({leftSamples, rightSamples}, numSamples);
```

`Processor<チャンネル数, float/double>` は係数の直列処理、`SmoothProcessor` は旧・新の経路をクロスフェードします。後者は入力履歴の最大2048サンプルを新しいフィルターへ通し直して状態を作るので、段数が変わっても以前の段の状態が混入しません。履歴の再処理は係数を設定する音声ブロックで実行されます。フェード中の更新は最新の一件を待機させます。`DesignQueue` は送信スレッド1本と受信スレッド1本専用。音声処理中にメモリ確保やロックはありません。複数バンドを使うときは、バンドごとに Processor を直列に通してください。

`set()` と `process()` は同じ音声スレッドから呼びます。設計結果を別スレッドから `set()` へ直接渡さず、`DesignQueue` 等で受け渡してください。チャンネルのポインターを `nullptr` にすると、そのチャンネルの状態と入力履歴は進みません。`SmoothProcessor::reset()` は履歴をクリアし、フェード中・待機中なら最後に受理した係数を即時適用します。再初期化によって以前の設定に戻ることはありません。

`SmoothProcessor::set()` は、直近の要求と段数・使用中の係数が同じなら、そのまま成功を返して履歴再構築やフェードを省きます。`maxErrorDb` や未使用の係数スロットの違いは音声の変更として扱いません。ブロックの末尾でフェードが終わった場合も、次のブロック前に届いた新しい要求が古い待機要求より優先されます。

`decramp::isValid(design)` は、有効フラグ、段数、有限の係数・誤差値、極の安定性を確認します。`Processor::set()`、`SmoothProcessor::set()`、`DesignQueue::push()` も同じ検証を行い、不正な結果には `false` を返して現在の状態を保ちます。有効な0段の `Design` はユニティです。`maxErrorDb` の許容値は用途に応じて別に確認してください。

```cpp
decramp::Band notch{decramp::Type::notch, 8000., 3.};
decramp::Band tilt{decramp::Type::tilt, 1500., 0.70710678, 6.};
decramp::Band pass{decramp::Type::bandPass, 2000., 1.};
decramp::Band hp{decramp::Type::highPass, 80., 0.70710678};
hp.slopeDbPerOct = 96.;

// 実際のデジタル振幅カーブを UI に描くときに使用。
auto db = decramp::responseDb(coefficients, fs, 12000.);
```

JUCE のステレオ `AudioBuffer<float>` では、`leftSamples` と `rightSamples` に `buffer.getWritePointer(0)` と `buffer.getWritePointer(1)` を渡せます。

## カーブの意味

| 種類 | 係数の構成 | パラメーター |
| --- | --- | --- |
| ベル／シェルフ | 各2 biquad | `gainDb` は±24 dB、`q` はアナログ原型のQ |
| ローパス／ハイパス | 12 dB/oct当たり2 biquad、最大16 biquad | `slopeDbPerOct` は12～96。既定のQ=1/√2では整数段数が高次 Butterworth カーブ。`q` はその共振量を調整 |
| ノッチ | 2 biquad | `frequencyHz` に正確なゼロ、`q` は幅。残りの極・ゼロをアナログ原型の振幅にフィット |
| チルト | 16 biquad | 正の `gainDb` は高域を上げる。20 Hz～`fitTopHz` の両端におけるゲイン差が `gainDb`、`frequencyHz` が0 dBの支点。`q` は使用しない |
| バンドパス | 2 biquad | 2次HP＋2次LPのアナログ振幅を2 biquadにフィット。`frequencyHz` は幾何学的中心、`q` で帯域幅を設定し、中心を0 dBに正規化 |

整数スロープは高次 Butterworth の極を使います。既定のQでカットオフを1オクターブ通り過ぎると、48 dB/oct は約48 dB、96 dB/oct は約96 dB落ちます。段数間では各極を連続して動かし、端数の段を2 biquadでフィットするため、段が増減する境目でも周波数応答がつながります。**端数の傾きは指定した有限帯域での近似**で、無限に低い／高い周波数まで端数の dB/oct を維持しません。極端に高いQでは、端数スロープの誤差が0.5 dBを超える場合があります。`maxErrorDb` を確認してください。

端数段で初回の近似誤差が大きい場合は、別の初期係数からもう一度だけフィットし、検証点における最大誤差が小さい結果を採用します。低域や共振の強いローパス、整数スロープ付近のハイパスで、不要なピークやノッチに収束しにくくしています。設計時間は難しい設定で増えるため、係数設計はワーカースレッドで行ってください。

## 入力・精度

- サンプルレート44.1 kHz以上、周波数20 Hz以上かつナイキスト未満、Q 0.3～10。ローパス／ハイパスでは `gainDb` を使用しません。全体のカットオフを指定位置に合わせるため、内部の原型周波数は20 Hz未満になることがあります。必要な原型周波数がナイキストを超えると `valid=false` になります。バンドパスの上側アナログカットオフはナイキストを超えても設定できます。
- フィット上限は既定で20 kHz、`fitTopHz` で変更できます。ナイキスト以下にしてください。この上限より高い周波数の一致は保証しません。
- `maxErrorDb` はサンプル点での振幅誤差の目安で、全周波数の厳密な上限ではありません。ノッチ中心ではアナログとデジタルに共通するゼロを解析的に約分し、その極限での誤差を使います。浮動小数点の実処理で無限のノッチ深さを保証するものではありません。チルトの直線性は20 Hz～`fitTopHz`の対数周波数上で測ります。
- カット系の誤差評価は線形・対数間隔、原型の周辺、複素極・ゼロの周波数付近を調べます。ローパスでは20 Hz以下の不要な盛り上がりも検査します。
- `valid=true` は段数・係数の有限性・極の安定性の検査を通ったことを表し、特定の近似精度や出力レベルを保証しません。数値計算が成立しない場合や極が単位円に近すぎる場合は `valid=false` を返します。`responseDb()` は不正な設計・範囲外の入力にはNaN、厳密なゼロ点には負の無限大を返します。
- `responseDb()` は各段をスケールしてdB値を加算し、多段の極端なゲインや減衰でも途中の積によるオーバーフロー／アンダーフローを避けます。
- ベルとシェルフのゲイン0 dBはバイパス。位相はアナログ原型と一致しません。ハイパスは整数段のゼロによりDCを遮断し、ローパスはDCで0 dBです。
- 係数と内部状態は double。`design()` は段数分の有界な数値フィットを行い、96 dB/oct やチルトの設計には数ms～十数msかかる場合があります。ワーカースレッドでパラメーター変更時に呼び、GUIと音声のスレッドから直接呼ばないでください。`SmoothProcessor::set()` は音声スレッドで有界な入力履歴の再処理を行います。パラメーター操作中の切り替えには `SmoothProcessor` を使ってください。

## ビルドと検証

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDECRAMPEQ_BUILD_TESTS=ON -DDECRAMPEQ_BUILD_EXAMPLE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

アナログ原型の2次曲線は [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) の式に沿っています。

精度・音声処理の検証に加え、不正係数の拒否、リセットとチャンネル再接続、2スレッド間でのキュー転送、44.1～192 kHzの1,152通りのパラメータにおける極とインパルス応答をテストします。極端な設定には精度保証を設けず、`maxErrorDb` を確認する前提です。

[精度改善の測定結果と検証範囲](docs/validation.md)

設計と重複更新の時間を測る、外部依存なしのベンチマークもあります。時間のしきい値をテストの合否には使いません。

```sh
cmake -S . -B build -DDECRAMPEQ_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target decramp_benchmark
```

生成された `decramp_benchmark` を実行すると、各ケース41回の中央値・平均値・近似誤差をCSVで出力します。Visual Studioでは `build/Release/decramp_benchmark.exe` に生成されます。

## ライセンスと出自

MITライセンスです。単一ヘッダーにも全文を含めています。
[参照元](THIRD_PARTY.md)と[ライセンス・出自の確認記録](docs/license-audit.md)に、元ZIP、依存関係、限定的な公開コード検索の結果と確認範囲を記載しています。元のライブラリは別のAIタスクで新規作成したものと管理者から確認していますが、生成履歴の検証や、あらゆる第三者コードとの一致確認を完了したという意味ではありません。
