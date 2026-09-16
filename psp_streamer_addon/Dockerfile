FROM ghcr.io/home-assistant/amd64-base:3.21

LABEL \
    io.hass.type="app" \
    io.hass.arch="amd64"
RUN apk add --no-cache python3 ffmpeg fontconfig font-dejavu mkvtoolnix
WORKDIR /app
COPY rootfs /
COPY run.sh /run.sh
RUN chmod a+x /run.sh
CMD ["/run.sh"]
