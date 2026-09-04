FROM ghcr.io/home-assistant/amd64-base:3.21

LABEL \
    io.hass.type="app" \
    io.hass.arch="amd64"
RUN apk add --no-cache ffmpeg fontconfig font-dejavu
WORKDIR /app
COPY rootfs /
COPY run.sh /run.sh
RUN chmod a+x /run.sh
CMD ["/run.sh"]
