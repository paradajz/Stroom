# This image supplies compiler toolchains and launcher utilities, not the SDK.
FROM ghcr.io/ps2dev/ps2dev@sha256:6e7935b0c9b0e1b89e1dd81293ced20723af16937be22918072499862deda156 AS toolchains
FROM alpine:3.24.2

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000
ENV PS2DEV=/usr/local/ps2dev

COPY --from=toolchains /usr/local/ps2dev/ee /usr/local/ps2dev/ee
COPY --from=toolchains /usr/local/ps2dev/iop /usr/local/ps2dev/iop
COPY --from=toolchains /usr/local/ps2dev/dvp /usr/local/ps2dev/dvp
COPY --from=toolchains /usr/local/ps2dev/bin /usr/local/ps2dev/bin
COPY --from=toolchains /usr/local/ps2dev/share /usr/local/ps2dev/share

RUN apk add --no-cache bash build-base libjpeg-turbo-dev libpng-dev clang-extra-tools cmake git patch nodejs npm ffmpeg shadow pkgconf gmp mpc1 mpfr4 zstd-libs \
    && npm install --global prettier@3.9.9 \
    && addgroup -g "${USER_GID}" "${USERNAME}" \
    && adduser -D -u "${USER_UID}" -G "${USERNAME}" -s /bin/bash "${USERNAME}" \
    && chown -R "${USERNAME}:${USERNAME}" "${PS2DEV}"

USER ${USERNAME}
