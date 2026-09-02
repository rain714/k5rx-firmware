# K5RX technical overview

この文書は、K5RXを利用・改造する人向けに、外から見える動作とその背景にある設計をまとめたものです。

## ベースFirmware

K5RXはF4HWN v4.3を直接のベースとしています。Git history上の起点は:

`fbcf26d8e9811b135b7e2d97bdefebaa4b3ed9e0`

F4HWN由来の受信機能を維持しながら、受信専用用途向けにMemory/Scan/EEPROM/UIを拡張しています。

## Receive-only design

### 利用者から見える動作

K5RXの標準buildでは通常の送信操作を提供しません。

- PTTはMain画面でmomentary Monitorとして動作する
- TX用menu/UIは表示されない
- Scan中にPTT Monitorを使用しても、離すとScanへ戻る
- Broadcast FM中のPTTはFMを終了してMonitorへ移る

### 実装上の境界

送信専用state、TX UI、TX helper、PA制御などはcompile-timeで除外または無効化します。加えて、BK4819 register writeやPA enable経路にもTX/PA guardを置いています。

一方、local beepなど受信機として必要なaudio経路まで壊さないため、BK4819のTX系DSP blockに由来する処理を機械的にすべて削除しているわけではありません。

このためK5RXの「receive-only」はsoftware feature/safety boundaryであり、認証されたRF no-emission保証ではありません。

## EEPROM Schema 2

K5RXはstock/F4HWNとは別のEEPROM layoutを使用します。

```text
0x0000 ................................ 0x1DFF  K5RX mutable area
0x1E00 ................................ 0x1FFF  Factory / Calibration
```

主な契約:

| 項目 | 値 |
|---|---:|
| EEPROM size | 8192 bytes |
| Memory channels | 400 |
| Channel record | 8 bytes |
| Channel name | 10 bytes |
| Scan Lists | 3 |
| Banks | 8 |
| Mutable end | `0x1E00` |

### Schemaが合わない場合に利用者からどう見えるか

stock/F4HWNなど別layoutのEEPROMを残したままK5RXを起動しても、通常bootだけでK5RX Schemaへ勝手に変換しません。

その場合、受信自体は安全なdefault設定で起動できますが、K5RX設定を通常どおり保存できる状態ではありません。初回導入ではFull Factory Resetを明示実行してSchema 2を作成します。

この設計により、Firmwareを書き込んだだけで既存EEPROMを自動的に破壊することを避けています。

### Factory / Calibration protection

K5RXの通常設定保存、Memory/Bank保存、FM/Spectrum persistenceは`0x1E00`未満を使用します。

K5RX向けnormal-mode UART writeでもFactory / Calibration領域へのwriteを許可しない境界を設けています。Companion K5RX Toolsはさらに狭く、通常のMemory編集ではMemory/name/Bankに必要な領域だけを書き込みます。

Calibration領域は個体固有値を含み得るため、別個体のbackupを流用しないことを前提としています。

## 400-channel Memory model

各Memoryはcompactな8-byte recordと10-byte nameで保持します。

Recordには周波数や受信設定に加えて:

- Scan List 1〜3のbit mask
- Bank code

を格納します。

Bank codeは単一値なので、各Memoryが所属できるBankは0または1つです。Scan Listはbit maskなので、同じMemoryを複数Listへ登録できます。

Firmwareはboot時に400ch分のdecoded channel cacheをRAMへ展開し、通常MR、Scan、Fast Scan、Bank UIで共有します。EEPROMを各機能が個別にdecodeする構成を避けることで、処理速度とFlash使用量の両方を抑えています。

## Scan model

K5RXでは3つのScan Listを組み合わせ、6種類のscan scopeを提供します。

| scope | 条件 |
|---|---|
| `0` | List 1〜3のどれにも属さない |
| `1` | List 1に属する |
| `2` | List 2に属する |
| `3` | List 3に属する |
| `123` | List 1〜3のいずれかに属する |
| `All` | 有効な全Memory |

この構成により、従来のList 1〜3を維持しつつ、「未分類だけ」「何らかのListに入れたもの全部」「400ch全体」といった使い方ができます。

## Adaptive Fast Scan

Normal Scanは各Channelを通常の受信手順で順に確認します。

Fast Scanは、quietと判断できるChannelでは受信ICの再設定やsettle待ちを最小化し、短時間のRSSI/noise判定で次へ進みます。信号候補が見つかったChannelは通常の受信経路で確認します。

設計上のポイント:

- Fast pathだけで受信成立を最終決定しない
- quiet channelに使う時間を削る
- tune後のsettleをblocking delayではなく段階的に扱う
- noise referenceを利用して環境変化へ適応する
- Main画面へ約500 ms単位で`ch/s`を更新する

Fast Scanの実速度は周波数、modulation、信号数、受信環境によって変化します。

## Bank model

BankはMemoryの分類機能で、Scan Listとは独立しています。

Bank UIでは:

- 8 Bankから選択
- Bank所属Memoryを一覧
- Memoryを一時的にMain画面で受信確認
- Scan List 1〜3を個別toggle
- Bank内容をScan List 1〜3へLOADまたはADD

ができます。

`LOAD`は選択Bankを対象Scan Listの内容として反映し、`ADD`は既存Listを残してBank所属Memoryを追加します。

この構造により、例えばBankを「用途/地域による分類」、Scan Listを「今日Scanしたい組み合わせ」として別々に管理できます。

## FM Radio / Spectrum persistence

K5RXではFM Broadcast RadioとSpectrum Analyzerの保存領域もSchema 2内へ配置しています。旧layoutのFactory/Calibration側addressへ依存しないため、K5RXのmutable boundary内で設定を完結できます。

## Build profiles

`K5RX`が利用者向けの標準buildです。

`DEFAULT` profileはF4HWN由来コードへの意図しない影響を検出するためのdeveloper regression buildとして残しています。K5RX releaseとして配布するprofileではありません。

## Flash/RAM constraints

対象MCUではFlash/RAMに余裕が大きくありません。そのためK5RXでは、一般的なdesktop softwareのように抽象化や重複排除を無条件に増やすのではなく、生成されるcode sizeも設計判断に含めます。

具体的な開発方針は [`flash-size-policy.md`](flash-size-policy.md) を参照してください。
