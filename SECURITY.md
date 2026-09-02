# Security Policy

K5RXは組み込みFirmwareであり、書込み失敗や不適切なEEPROM操作によりRadioが起動しなくなる可能性があります。

## Before reporting

- Hardware型番とMCUを確認してください。
- EEPROM全体のbackupを保持してください。
- 問題がK5RX official profileで再現するか確認してください。
- Calibration領域を他個体からコピーしないでください。

## Receive-only scope

K5RXの`DISABLE_TX`はsoftware上のcompile-time TX removal / hardeningです。RF無放射を認証・保証するsecurity boundaryではありません。

TX guardのbypass、unexpected PA enable、Factory/Calibration領域への意図しないwrite等は重要な問題として扱います。

## Reporting

初回public release時にGitHubのprivate vulnerability reportingを有効化することを推奨します。公開Issueへdevice固有backupや個人情報を添付しないでください。
