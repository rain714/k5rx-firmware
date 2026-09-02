# K5RX Firmware

K5RX は Quansheng UV-K5 / UV-K6 **V1（DP32G030）を受信機として使いやすくするためのカスタムFirmware**です。

> [English README](README.en.md)

送信機能をソフトウェア上で取り除いた構成を基本とし、400 Memory、Bank、Fast Scan、Spectrum Analyzer、FM Broadcast Radioなど、受信・探索用途を中心に機能を構成しています。

## 対応機種

対応:

- Quansheng UV-K5 V1 系
- Quansheng UV-K6 V1 系
- MCU: DP32G030

非対応:

- UV-K5 V3 系
- UV-K1 系
- DP32G030以外の派生Hardware

**対応外の機種には書き込まないでください。**

## K5RXでできること

### 受信中心の操作

- 通常のVFO / Memory受信
- PTTを押している間だけスケルチを開く **Monitor**
- TINY / CLASSIC GUI
- RX Timer

K5RXでは通常の送信操作を提供しません。PTTは通常画面ではMonitorとして動作し、離すと元の受信状態へ戻ります。Scan中に使った場合も、PTTを離すとScanへ戻ります。

### 400 MemoryとScan

- 400 Memory channels
- 10文字のMemory name
- 3つのScan List
- 6種類のScan対象
  - List未所属
  - List 1
  - List 2
  - List 3
  - List 1〜3のいずれかに所属
  - 全Memory
- 通常Scan / Adaptive Fast Scan

Fast Scanを選ぶと、信号のないChannelをより短時間で通過できるように動作します。Scan中はMain画面に実測の `ch/s` が表示されます。

### 8 Banks

400 Memoryを8つのBankへ分類できます。Bank画面ではBank内のMemoryを一覧し、その場で受信確認したり、Scan List 1〜3への所属を変更したりできます。

各Memoryが所属できるBankは最大1つです。Bankとは別にScan List 1〜3を組み合わせて利用できます。

### 受信補助機能

- FM Broadcast Radio
- Spectrum Analyzer
- AM受信を含むF4HWN由来の受信機能

詳しい操作は [`docs/features.md`](docs/features.md) を参照してください。

## 最初に使うとき

K5RXはstock / 通常のF4HWNとは異なるEEPROM形式を使用します。**Firmwareを書き込む前に、現在のEEPROM全体を必ずbackupしてください。**

推奨手順:

1. 現在のEEPROMを8192-byte RAWとしてbackupする。
2. K5RX firmwareを書き込む。
3. K5RXとして起動したことを確認する。
4. **Full Factory Reset**を実行し、K5RX用EEPROMを初期化する。
5. 本体またはK5RX ToolsでMemory / Bankを設定する。
6. 普段使う受信、Scan、Fast Scan等を確認する。

EEPROMのbackup・移行・復旧については [`docs/eeprom-migration.md`](docs/eeprom-migration.md) を先に確認してください。

## MemoryをPCから編集する

Companion project **K5RX Tools (`k5rx-tools`)** では、K5RXのMemory / BankをPCから管理できます。

主な用途:

- Radio EEPROMのRAW backup
- 400 MemoryのCSV import / export
- Bank名・Bank所属の編集
- Web Serialを利用したブラウザ版Memory Manager
- CLIによるread / edit / write / verify

FirmwareとToolsの対応関係は [`docs/compatibility.md`](docs/compatibility.md) にまとめています。

## 「受信専用」について

K5RXの標準buildでは、送信画面・送信処理・PA制御などをソフトウェア上で除外または無効化し、低層側にもTX / PAを有効化しにくくする防御を入れています。

ただし、これは**ソフトウェアによる送信機能の除去**です。RF無放射を認証・保証するものではなく、Hardware故障、未知のsilicon behavior、外部改造などまで保証するものではありません。使用地域の法令・利用条件に従ってください。

技術的な構成は [`docs/technical-overview.md`](docs/technical-overview.md) を参照してください。

## EEPROM互換性

K5RXは **K5RX EEPROM Schema 2** を使用します。

| 項目 | K5RX Schema 2 |
|---|---:|
| EEPROM size | 8192 bytes |
| Memory channels | 400 |
| Channel record | 8 bytes |
| Channel name | 10 bytes |
| Banks | 8 |
| K5RX設定領域 | `0x0000..0x1DFF` |
| Factory / Calibration | `0x1E00..0x1FFF` |

stock / 通常のF4HWN EEPROMをそのままK5RX設定として使用することはできません。逆に、K5RXから別Firmwareへ戻す場合も、対応するEEPROM backupまたは移行手順を使用してください。

通常のK5RX設定保存やK5RX ToolsのMemory編集はFactory / Calibration領域を書き換えない設計です。

## SourceからBuildする

K5RXがdefault profileです。

```bash
make clean all
```

DockerまたはApple `container` CLIを利用する場合:

```bash
sh ./build.sh
```

生成物:

```text
compiled-firmware/
├── k5rx-firmware.bin
├── k5rx-firmware.packed.bin
└── SHA256SUMS
```

詳しくは [`docs/build.md`](docs/build.md) を参照してください。

## Documentation

- [`docs/features.md`](docs/features.md) — 主な機能と操作
- [`docs/eeprom-migration.md`](docs/eeprom-migration.md) — EEPROM backup / 初回導入 / recovery
- [`docs/compatibility.md`](docs/compatibility.md) — Hardware / EEPROM / Tools互換性
- [`docs/technical-overview.md`](docs/technical-overview.md) — 受信専用設計、EEPROM、Scan / Bankの技術概要
- [`docs/build.md`](docs/build.md) — Source / container build
- [`docs/flash-size-policy.md`](docs/flash-size-policy.md) — Firmware開発時のFlash容量方針

## Origin / Credits

K5RXはF4HWN firmwareを直接のベースとし、UV-K5 open firmware ecosystemの成果を利用しています。F4HWN / armel、Egzumer、OneOfEleven、DualTachyon、fagciをはじめとするcontributorsの成果に基づいています。

Git history上のK5RXの起点はF4HWN v4.3 commit `fbcf26d8e9811b135b7e2d97bdefebaa4b3ed9e0` です。詳細なattributionは [`NOTICE`](NOTICE) を参照してください。

## License

Apache License 2.0。詳細は [`LICENSE`](LICENSE) を参照してください。
