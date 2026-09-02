# K5RX EEPROM backup / migration / recovery

K5RXはstock firmwareや通常のF4HWNとは異なるEEPROM layoutを使用します。**K5RXを書き込む前に、現在のEEPROM全体を必ずbackupしてください。**

この文書では、初回導入、Memory移行、別Firmwareへ戻す場合の考え方を利用者向けにまとめます。

## 最重要: 先に8192-byte RAW backupを保存する

Firmwareを書き換える前に、現在のRadioからEEPROM全体を読み出し、8192 bytesのRAW fileとして保存してください。

推奨:

1. EEPROM全体をRAWで保存する。
2. file sizeが8192 bytesであることを確認する。
3. SHA256等のhashを記録する。
4. PC以外にもcopyを保存する。
5. その後K5RXを書き込む。

このbackupは、元Firmwareへ戻すときや設定を誤って変更したときの最も確実な復旧材料です。

## K5RXのEEPROM

K5RX Schema 2の主な仕様:

| 項目 | 値 |
|---|---:|
| EEPROM size | 8192 bytes |
| Memory channels | 400 |
| Channel record | 8 bytes |
| Channel name | 10 bytes |
| Scan Lists | 3 |
| Banks | 8 |
| K5RX設定領域 | `0x0000..0x1DFF` |
| Factory / Calibration | `0x1E00..0x1FFF` |

Factory / Calibration領域には個体固有の調整値が含まれる可能性があります。他個体のbackupで置き換えないでください。

## 初めてK5RXを導入する

推奨手順:

1. 元のEEPROMをbackupする。
2. K5RX firmwareを書き込む。
3. K5RXとして起動することを確認する。
4. **Full Factory Reset**を実行する。
5. MemoryやBankを設定する。
6. 普段利用する受信機能を確認する。

### なぜFull Factory Resetが必要か

K5RXは別FirmwareのEEPROMを通常boot時に自動変換しません。

K5RX用Schemaがない状態でもRadioはdefault設定で起動できますが、その状態はK5RX設定を通常どおり保存して使うための初期化済み状態ではありません。

Full Factory Resetを明示実行することで、K5RX Schema 2の設定領域を作成します。

これは「Firmwareを書き込んだだけで既存EEPROMが勝手にK5RX形式へ書き換わる」ことを避けるための仕様です。

## Memory / Bankを移行する

K5RXでは400 Memory、3 Scan Lists、8 Banksを利用できます。

大量のMemoryを移行・編集する場合は **K5RX Tools (`k5rx-tools`)** を推奨します。

K5RX Toolsでは:

- RadioからRAW EEPROMをread
- RAW backupを保存
- CSVへMemoryをexport
- CSVから400 Memoryをimport
- Bank名・Bank所属を編集
- Radioへwriteした後にread-back verify

といった操作ができます。

stock/F4HWN EEPROMのbinary layoutをK5RXへそのままcopyするのではなく、対応ツールやCSVを介して必要なMemory情報を移行してください。

## K5RX設定を壊した場合

MemoryやBankの内容を誤って変更した場合は、すぐにCalibration領域まで含む全面restoreを行うのではなく、まず現在のRAWをbackupしてください。

その後:

1. 現在のEEPROMを新しいRAWとして保存する。
2. 正常時のbackupとの差分を確認する。
3. Memory/Bankだけの問題ならK5RX Toolsで必要な範囲を戻す。
4. 完全restoreが必要な場合だけ、信頼できるEEPROM programmer/recovery手順を使う。

K5RXの通常設定保存やK5RX ToolsのMemory編集はFactory / Calibration領域を書き込まないように設計されています。

## 別Firmwareへ戻す

K5RXからstock/F4HWN等へ戻す場合、K5RX Schema 2のEEPROMをそのまま利用できるとは限りません。

推奨手順:

1. 戻したいFirmwareを書き込む。
2. そのFirmwareで取得していたEEPROM backupをrestoreするか、そのFirmware向けの初期化/移行手順に従う。
3. Calibration領域は元の個体の値を維持する。
4. 受信・表示・設定保存を確認する。

互換性のないEEPROM layoutを推測で部分編集しないでください。

## Factory / Calibration領域を完全restoreする場合

`0x1E00..0x1FFF`を含む完全restoreは、通常のK5RX設定変更とは別のrecovery操作として扱ってください。

- restore元が**同じRadio個体**のbackupであることを確認する
- backup fileのsize/hashを確認する
- 書込み後にread-backして一致を確認する

誤ったCalibration dataは受信性能やRadioの動作へ影響する可能性があります。

## 関連資料

- [`compatibility.md`](compatibility.md) — Firmware / EEPROM / Tools互換性
- [`technical-overview.md`](technical-overview.md) — Schema 2と保護境界の技術的な説明
- [`features.md`](features.md) — 400 Memory / Scan List / Bankの使い方
