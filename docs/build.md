# Build K5RX

K5RXは通常の`make`、Docker、Apple `container` CLIのいずれでもbuildできます。

利用者がK5RXをsourceからbuildする場合は、通常はdefault profileのままで構いません。

## Native build

```bash
make clean all
```

これは次と同じです。

```bash
make clean all BUILD_PROFILE=K5RX
```

必要なtoolchainがhostへ導入済みの場合に利用できます。

## Container build

Hostへbuild toolchainを直接入れたくない場合は、repository付属のwrapperを利用できます。

```bash
sh ./build.sh
```

`build.sh`は利用可能なruntimeを次の順で検出します。

1. Apple `container` CLI
2. Docker

明示的に選択する場合:

```bash
CONTAINER_RUNTIME=container sh ./build.sh
CONTAINER_RUNTIME=docker sh ./build.sh
```

version文字列を指定する場合:

```bash
K5RX_VERSION=v0.1.0 sh ./build.sh
```

## Build output

生成物は`compiled-firmware/`へ出力されます。

```text
compiled-firmware/
├── k5rx-firmware.bin
├── k5rx-firmware.packed.bin
└── SHA256SUMS
```

通常のFirmware書込みでは、使用するprogrammerの仕様に応じて`.packed.bin`を利用します。

配布fileを使用する場合は、releaseに含まれる`SHA256SUMS`とhashを照合してください。

## Build image

Container buildではrepository rootの`Dockerfile`から専用build imageを作成します。

Wrapperは他projectのDocker/container image、volume、containerをglobal pruneしません。

既存のbuild image名を明示する場合:

```bash
BUILD_IMAGE=k5rx-build sh ./build.sh
```

## Developer regression build

F4HWN由来部分への意図しない影響を確認するため、`DEFAULT` profileもbuildできます。

```bash
make clean all BUILD_PROFILE=DEFAULT
```

または:

```bash
BUILD_PROFILE=DEFAULT sh ./build.sh
```

`DEFAULT`はdeveloper向けregression profileであり、K5RXとしてRadioへ書き込むためのrelease profileではありません。

## Reproducible builds

同じsource revision、version、toolchain/container imageから同じbinaryを生成できることをrelease品質の一部として扱っています。

Release artifactを検証する場合は:

1. 対象tag/commitをcheckoutする。
2. releaseと同じversionを指定してbuildする。
3. `.bin` / `.packed.bin`のSHA256を比較する。

CIでもK5RX build、size guard、DEFAULT regression buildを実行します。

## Firmware size

対象MCUではFlash容量が限られています。機能追加や改造を行う場合は、build後の`text / data / bss`も確認してください。

K5RX側の考え方は [`flash-size-policy.md`](flash-size-policy.md) にまとめています。
