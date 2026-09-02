# K5RX 主な機能と操作

この文書は、K5RXを初めて使う人向けに、本体上で見える機能と基本操作をまとめたものです。

## PTT: momentary Monitor

K5RXではPTTから送信しません。通常画面でPTTを押している間はMonitorとして動作し、スケルチを開いて受信音を確認できます。

- PTTを押す: Monitor
- PTTを離す: 元の受信状態へ戻る
- Scan中: PTTを離すとScanへ戻る

Broadcast FMを使用中にPTTを押すとFM Radioを終了してMonitorへ移ります。この場合はPTTを離してもFM Radioへ自動復帰しません。

## Memory

K5RXでは **M001〜M400** の400 Memoryを利用できます。

各Memoryには主に以下を保存できます。

- 受信周波数
- modulation / bandwidth等の受信設定
- 10文字のname
- Scan List 1〜3への所属
- Bank 1〜8のいずれか1つへの所属

PCで多数のMemoryを編集する場合はK5RX Toolsの利用を推奨します。

## Scan List

MemoryはScan List 1〜3へ個別に登録できます。1つのMemoryを複数のScan Listへ同時に登録することもできます。

Scan対象は次の6種類から選べます。

| 表示 | Scan対象 |
|---|---|
| `0` | List 1〜3のどれにも所属しないMemory |
| `1` | Scan List 1 |
| `2` | Scan List 2 |
| `3` | Scan List 3 |
| `123` | List 1〜3のいずれかに所属するMemory |
| `All` | すべての有効なMemory |

List 1〜3では従来どおりpriority channelも設定できます。

## Normal Scan / Fast Scan

Scan modeはNormalとFastを切り替えられます。

### Normal Scan

通常の受信設定・判定を使ってMemoryを順番に確認します。互換性と安定性を優先する場合はこちらを使用します。

### Fast Scan

Fast Scanは、明らかに信号がないChannelでの待ち時間を短縮し、候補が見つかった場合は通常の受信判定へつなぐことで、400 Memoryの探索を高速化します。

Fast Scan中はMain画面に現在の処理速度が `ch/s` で表示されます。電波状況、modulation、Memory内容などによって実際の速度は変化します。

## Bank

K5RXには8つのBankがあります。BankはMemoryを用途別にまとめるための分類です。

例:

- Airband
- Local
- Railway
- Emergency
- Amateur RX

各Memoryが所属できるBankは最大1つですが、Scan List 1〜3はBankとは独立して設定できます。

### Bank画面を開く

Function actionの `BANK` からBank画面を開きます。

Bank一覧では:

- `1`〜`8`: Bankを直接選択
- `UP` / `DOWN`: Bankを移動
- `MENU`: 選択したBankのMemory一覧を開く
- `F`: Bulk操作を開く
- `EXIT`: Bank画面を閉じる

### Bank内のMemory一覧

- `UP` / `DOWN`: 前後のMemoryへ移動
- `*`: 前のpage
- `F`: 次のpage
- `1` / `2` / `3`: 選択中MemoryのScan List 1 / 2 / 3所属をtoggle
- `MENU`: 選択中MemoryをMain画面で受信確認
- `EXIT`: Bank一覧へ戻る

Main画面で受信確認中は:

- `MENU`: そのMemoryを選択した状態で通常受信へ確定
- `EXIT`: Bank画面へ戻る

### Bulk操作

Bank一覧で `F` を押すと、選択BankをScan Listへ一括反映できます。

- `LOAD 1/2/3`: 対象Scan Listの内容を、そのBankのMemoryだけに置き換える
- `ADD 1/2/3`: 既存Scan Listを残したまま、そのBankのMemoryを追加する

Bankを「分類」、Scan Listを「実際にScanする集合」として使い分けると便利です。

## FM Broadcast Radio

FM Broadcast Radioを利用できます。K5RX用EEPROMではFM Radioの設定・保存領域もK5RX layout内に配置されています。

PTTはFM Radioの一時Monitorではなく、FM Radioを終了して通常のMonitorへ移る操作になります。

## Spectrum Analyzer

Spectrum Analyzerを利用できます。設定保存先はK5RXの設定領域内にあり、Factory / Calibration領域を使用しません。

## GUI

Main画面はTINY / CLASSIC表示を切り替えられます。表示密度や見やすさに応じて選択してください。

## RX Timer

受信側のTimer表示を利用できます。K5RXでは送信Timer部分はありませんが、受信時間の表示機能は残しています。

## Memory / BankをPCから管理する

大量のMemory登録やBank整理では、本体だけで操作するよりK5RX Toolsが便利です。

K5RX ToolsではRAW backup、CSV import/export、Bank編集、Web Serialによるread/write/verifyを行えます。

EEPROMを書き換える前には [`eeprom-migration.md`](eeprom-migration.md) のbackup手順も確認してください。
