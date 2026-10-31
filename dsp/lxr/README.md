# dsp/lxr — LXR-02 ファームウェアの音源部分の移植

このディレクトリと `third_party/lxr/` は、Erica Synths の LXR ファームウェア
（https://github.com/erica-synths/lxr-desktop-app 、コミットは
`third_party/lxr/UPSTREAM_COMMIT`）の **DRUM ボイスと FX バス**を LUXOR で動かすためのものです。
ライセンスは **GPL-3.0-only**（追加条項は `third_party/lxr/NOTICE.md`）。
これは Erica Synths の公式ソフトウェアではなく、改変版です（NOTICE.md 7(c)）。
以下が元のソースとの違いのすべてです。

## 構成

| 場所 | 中身 | 元のソースとの関係 |
|---|---|---|
| `third_party/lxr/` | ファームウェアのファイル | **無改変のコピー**（改行も元の CRLF のまま） |
| `lxr_sources.txt` | `dsp.so` に入れるファームウェアのファイルの一覧 | ビルドと基準テストが共通で読む唯一の一覧 |
| `lxr_drum.c` / `lxr_drum.h` / `lxr_params.h` | DRUM ボイス 1 つ分と FX バス | `DrumVoice.c`・`lfo.c`・`modulationNode.c`・`MidiParser.c`（DRUM 1 と FX の部分）・`mixer.c`（ボイス 1 の部分）・`FxBus.c` からの改変 |
| `patched/Oscillator.c`、`patched/Distortion.c` | 範囲外読み出しを直したファイル | 元のファイル＋各 1 種類の修正（各ファイルの先頭に記載） |
| `compat/` | STM32 の HAL / CMSIS と、一部のファームウェアのヘッダーの代わり | 下記 |

## lxr_drum.c で変えたこと

ファームウェアは 3 つの DRUM ボイスをグローバル配列（`voiceArray[]`、`velocityModulators[]`、
`fxBus`、`FX_BUFFER`、`mixer_decimation_*`）で持ち、音色の変更は MIDI CC を
`midiParser_ccHandler()` に通して行います。Move では LUXOR を最大 4 スロット同時に使えるため、
**これらを 1 インスタンス（`lxr_drum_t`）にまとめ直した**のが主な変更です。各関数の中身
（計算・定数・処理の順序）は元の文のままで、変えたのはグローバル変数をインスタンスの
フィールドに置き換えた部分だけです。関数ごとの違いはソース中のコメントに書いてあります。

そのほかの違い:

- **ハードウェア乱数** `GetRngValue()` は、インスタンスごとの xorshift32 疑似乱数です。
- **テンポ** `seq_getBpm()` は Move のテンポ（小数あり）。本体は整数 BPM です。
- **ベロシティ**は常に 127。ベロシティ変調ノードは入れていません。
- **LFO の掛け先**はこのボイスのパラメーターと、選んでいる FX の P1〜P4 です（本体の
  `modTargets[]` の DRUM 1 の部分から、ベロシティ・LFO 自身・ボイス選択を除いたもの）。
- **FX の設定**は FX の種類ごとに持ちます（本体は 4 つの値を全 FX で共有）。
  Delay と Ping-Pong はそれぞれ専用のディレイメモリを持ちます。
- **FX の切り替え**は 10 ms のクロスフェードでつなぎ、Delay / Ping-Pong を選び直したときは
  ディレイメモリを空にします（本体はメモリを消さないため、前の音が再生されます）。
- **Ring の波形変更**のあと、すぐにその波形用の音程計算をします（`osc_setFreq()`）。
- **出力の経路**は本体の `MIXER_ROUTING_FX_BUS` → `MIXER_ROUTING_DAC1_STEREO` に固定です。

## compat/

- `stm32f7xx_hal.h`: `__QADD16`・`__QSUB16`・`__SSAT`・`__PKHBT`・`__CLZ` を、ARMv7E-M の
  命令と同じ結果になる C で書いたもの。`__fp16` は x86 では `_Float16`。aarch64 では
  `fmod` を glibc 2.17 版に固定します（Move の glibc 2.34 に無い 2.38 版を避けるため）。
- `Soundengine/config.h`: 元の `config.h` の設定値そのままで、`REAL_FS` だけ 44100
  （本体は I2S の分周比の都合で 44140 Hz）。DTCM / ITCM の配置指定は空。
- `Soundengine/DSPAudio/FX/FxRamBuffer.h`: `FX_BUFFER` を、各 `Delay` 構造体の直後に置いた
  インスタンスごとのメモリとして参照します（`Delay.c` は無改変のまま）。
- そのほか（`globals.h`、`ParameterArray.h`、`MidiParser.h`、`sequencer.h`、`Samples.h`、
  `main.h`）は、DSP のファイルが使う定義だけを残したものです。

## 検証（tests/lxr_ref/）

`ref_main.c` は、ファームウェアの `MidiParser.c`・`DrumVoice.c`・`lfo.c`・`modulationNode.c`・
`ParameterArray.c`・`FxBus.c` を**無改変のまま**コンパイルし、本体のフロントパネルと同じ形の
CC メッセージでパラメーターを設定して DRUM 1 を FX バス経由で鳴らす基準レンダラーです
（本体の `mixer.c` は GPIO を読むので、ボイス 1 の部分だけを書き写しています）。
`port_main.c` は同じ 24 のシーン（`scenarios.h`）を移植版で鳴らします。
`run.sh` が両方をビルドして全サンプルを比較し、すべて一致することを確認します
（`SANITIZE=1` で ASan + UBSan 付き）。

```bash
tests/lxr_ref/run.sh
```

比較できないもの: 本体の実機（STM32F765）との比較はしていません。浮動小数点の演算は
FMA への融合を禁止（`-ffp-contract=off`）して、PC・基準・Move で同じ結果になるようにしています。
範囲外の浮動小数→整数変換（元のコードにあります）は、ARM（本体）と AArch64（Move）では
どちらも飽和変換で同じ結果ですが、x86 のテストビルドだけは結果が異なります。
