@echo off
setlocal

rem Native Windows build for users who already have GNU Arm Embedded Toolchain,
rem GNU Make and Python/crcmod installed in PATH.
if "%K5RX_VERSION%"=="" set K5RX_VERSION=dev
if "%BUILD_PROFILE%"=="" set BUILD_PROFILE=K5RX

make clean all BUILD_PROFILE=%BUILD_PROFILE% K5RX_VERSION=%K5RX_VERSION% || exit /b 1

if /I "%BUILD_PROFILE%"=="K5RX" (
  if not exist compiled-firmware mkdir compiled-firmware
  copy /Y f4hwn.bin compiled-firmware\k5rx-firmware.bin >NUL
  copy /Y f4hwn.packed.bin compiled-firmware\k5rx-firmware.packed.bin >NUL
  echo Build complete: compiled-firmware\k5rx-firmware.packed.bin
) else (
  echo DEFAULT regression build complete.
)

arm-none-eabi-size f4hwn
endlocal
