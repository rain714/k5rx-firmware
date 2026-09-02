# K5RX compatibility

K5RXを利用する際に確認すべきHardware、EEPROM、K5RX Toolsの互換性をまとめます。

## Hardware

| Hardware | 対応 |
|---|---|
| UV-K5 V1 / DP32G030 | 対応 |
| UV-K6 V1 / DP32G030 | 対応 |
| UV-K5 V3 | 非対応 |
| UV-K1 | 非対応 |
| その他のMCU/platform variant | 明示的に記載されない限り非対応 |

外観が似ていても内部Hardwareが異なるvariantがあります。MCU/platformを確認できない機種へ書き込まないでください。

## EEPROM

K5RXは **EEPROM Schema 2** を使用します。

| 項目 | Schema 2 |
|---|---:|
| EEPROM size | 8192 bytes |
| Memory channels | 400 |
| Channel name | 10 bytes |
| Scan Lists | 3 |
| Banks | 8 |
| K5RX mutable end | `0x1E00` |

stock firmwareや通常のF4HWNとはEEPROM layoutが異なります。

- K5RXへ移行する前に元EEPROMをbackupする
- 初回K5RX導入時はFull Factory ResetでSchema 2を初期化する
- 別Firmwareへ戻すときは、そのFirmwareに対応したEEPROM backup/初期化手順を使う

詳細は [`eeprom-migration.md`](eeprom-migration.md) を参照してください。

## K5RX Tools

K5RX ToolsはFirmwareと同じSchema contractを検証してからwriteします。

現在の組み合わせ:

| K5RX Firmware | EEPROM | K5RX Tools |
|---|---|---|
| Schema 2対応build | Schema 2 | Schema 2対応版 |

ToolsがSchema不一致と表示した場合は、互換性が確認できるまでwriteしないでください。

K5RX Toolsの通常Memory編集はMemory/name/Bankに必要な範囲だけを書き込み、Factory / Calibration領域はwrite対象にしません。

## Firmware build profile

利用者向けに使用するbuild profileは `K5RX` です。RepositoryのdefaultもK5RXです。

```bash
make clean all
```

`DEFAULT` はF4HWN由来コードのregression確認用に残しているdeveloper profileで、K5RXとして配布・利用するためのprofileではありません。

## VersionとSchema

Firmware versionとEEPROM Schema versionは別の概念です。

Firmwareが更新されてもSchema 2のままであれば、同じSchemaをサポートするK5RX ToolsとMemory dataを継続利用できます。将来Schemaが変更される場合は、Firmware release notesとmigration手順で明示します。

互換性が不明な組み合わせでは、先にRAW EEPROM backupを取得し、推測でwriteしないことを推奨します。
