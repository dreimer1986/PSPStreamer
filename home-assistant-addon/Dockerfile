ARG BUILD_FROM
FROM ${BUILD_FROM}
RUN apk add --no-cache ffmpeg fontconfig font-dejavu
WORKDIR /app
COPY rootfs/app/psp_streamer ./psp_streamer
COPY rootfs/app/static ./static
COPY run.sh /run.sh
RUN chmod a+x /run.sh
CMD ["/run.sh"]
