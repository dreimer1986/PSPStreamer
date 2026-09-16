FROM python:3.14-slim

# Intel's full media kernels are needed for bitrate-controlled AVC encoding
# on e.g. Kaby Lake. The free-only driver exposes CQP there, unsuitable for
# the PSP's constrained WLAN bitrate. No host package sources are changed.
RUN sed -i 's/^Components: main$/Components: main non-free/' /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get install -y --no-install-recommends ffmpeg fontconfig fonts-dejavu-core mkvtoolnix intel-media-va-driver-non-free mesa-va-drivers vainfo \
    && fc-cache -f \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY psp_streamer ./psp_streamer
COPY static ./static
ENV MEDIA_ROOTS=/media PORT=8091 MAX_TRANSCODES=1 PSP_STREAMER_SETTINGS_DIR=/data
VOLUME ["/data"]
EXPOSE 8091
CMD ["python", "-m", "psp_streamer.server"]
