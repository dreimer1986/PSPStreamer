#!/usr/bin/with-contenv bashio
set -euo pipefail
export MEDIA_ROOTS=/media
export PSP_STREAMER_DOWNLOAD_DIR=/data/downloads
export PSP_STREAMER_RADIO_DIR=/data
export PORT="$(bashio::config 'port')"
export MAX_TRANSCODES="$(bashio::config 'max_transcodes')"
export PSP_STREAMER_PASSWORD="$(bashio::config 'password')"
export PSP_STREAMER_TLS_CERT="$(bashio::config 'tls_cert')"
export PSP_STREAMER_TLS_KEY="$(bashio::config 'tls_key')"
bashio::log.info "Starting PSP Streamer on port ${PORT}; media root is /media (read-only)."
exec python3 -m psp_streamer.server
