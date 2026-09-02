# Flash size policy

K5RXの対象MCUではFlash/RAM容量が限られているため、Firmware sizeを通常の品質指標の1つとして扱います。

この文書はK5RXを改造・contributeする人向けの方針です。

## 基本方針

1. 機能変更前後の`text / data / bss`を確認する。
2. 受信機能を削る前に、TX-only、到達不能、重複処理の削減余地を調べる。
3. debug / capture / experiment用codeは標準buildへ常駐させない。
4. EEPROM decodeやchannel lookupなど複数機能で同じ処理を行う場合は、Flash/RAMのtrade-offを測定した上で共有化を検討する。
5. 汎用化・抽象化そのものを目的にcode sizeを増やさない。
6. K5RXだけでなく`DEFAULT` regression buildへの影響も確認する。

## なぜFlash sizeを重視するか

K5RXは400 Memory、Fast Scan、Bank UI、Spectrumなど複数の受信機能を限られたFirmware領域へ収めています。

数十〜数百byteの増加でも積み重なると新機能やbug fixの余地を失うため、「上限に収まっているか」だけでなく、増加量に対して得られる機能価値が妥当かを確認します。

## 最適化の優先順位

原則として次の順で検討します。

1. 不要なTX-only code
2. 到達不能・重複code
3. 同じdataの重複decode / cache
4. debug / diagnostic code
5. 表現やcontrol flowの小さな簡素化
6. 最後に、利用者が使う受信機能の削減

利用者向け機能を削って数byteを得るより、不要な実装を先に整理する方針です。

## RAMとのtrade-off

Flash削減のためにRAM cacheを使う場合も、RAM消費に対する効果を測定します。

例えばK5RXでは400 Memoryのdecoded channel情報を通常MR、Scan、Fast Scan、Bank UIで共有しています。各機能がEEPROMを別々にread/decodeする実装を避けることで、処理速度とcode sizeの両方に利点があります。

一方、同じ情報をraw/decodedの両方で大きく保持してもFlash削減が小さい場合は、単純な構成を優先します。

## CI / review

CIではK5RX buildに対するsize guardと、F4HWN由来部分の`DEFAULT` regression buildを実行します。

変更をreviewする際は、必要に応じて次を記録します。

```text
before: text=... data=... bss=...
after:  text=... data=... bss=...
delta:   text=... data=... bss=...
```

特に新機能でFlashが増える場合は、その増加が何のためか説明できる状態を推奨します。

## 数値について

Firmware sizeはcompiler、feature set、source revisionによって変わります。過去の開発途中のsizeを固定値として仕様化せず、対象commitを実際にbuildした値を基準にしてください。

Releaseではそのrelease artifactのbuild結果とsize guardを一次情報とします。
