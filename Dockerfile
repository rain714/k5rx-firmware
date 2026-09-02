ARG ALPINE_TAG=3.22
FROM alpine:${ALPINE_TAG}

# Reproducible firmware build toolchain. Project sources are mounted at /work
# by build.sh / CI rather than copied into the image.
RUN apk add --no-cache \
      bash \
      build-base \
      gcc-arm-none-eabi \
      newlib-arm-none-eabi \
      python3 \
      py3-crcmod

WORKDIR /work
