@echo off
setlocal

if "%K5RX_VERSION%"=="" set K5RX_VERSION=dev
if "%BUILD_PROFILE%"=="" set BUILD_PROFILE=K5RX

if not exist compiled-firmware mkdir compiled-firmware

docker build -t k5rx-build . || exit /b 1
docker run --rm --network none -v "%CD%:/work" -w /work k5rx-build sh -c "make clean all BUILD_PROFILE=%BUILD_PROFILE% K5RX_VERSION=%K5RX_VERSION%" || exit /b 1

if /I "%BUILD_PROFILE%"=="K5RX" (
  copy /Y f4hwn.bin compiled-firmware\k5rx-firmware.bin >NUL
  copy /Y f4hwn.packed.bin compiled-firmware\k5rx-firmware.packed.bin >NUL
  echo Build complete: compiled-firmware\k5rx-firmware.packed.bin
) else (
  echo DEFAULT regression build complete.
)

endlocal
