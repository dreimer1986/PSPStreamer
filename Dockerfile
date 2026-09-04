FROM python:3.14-slim

RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg fontconfig fonts-dejavu-core \
    && fc-cache -f \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY psp_streamer ./psp_streamer
COPY static ./static
ENV MEDIA_ROOTS=/media PORT=8091 MAX_TRANSCODES=1
EXPOSE 8091
CMD ["python", "-m", "psp_streamer.server"]
