#!/usr/bin/with-contenv bashio
set -euo pipefail
export MEDIA_ROOTS=/media
export PORT="$(bashio::config 'port')"
export MAX_TRANSCODES="$(bashio::config 'max_transcodes')"
export PSP_STREAMER_PASSWORD="$(bashio::config 'password')"
export PSP_STREAMER_TLS_CERT="$(bashio::config 'tls_cert')"
export PSP_STREAMER_TLS_KEY="$(bashio::config 'tls_key')"
export PSP_STREAMER_ACCELERATION="$(bashio::config 'acceleration')"
export PSP_STREAMER_VAAPI_DEVICE="$(bashio::config 'vaapi_device')"
bashio::log.info "Starting PSP Streamer on port ${PORT}; media root is /media (read-only)."
exec python3 -m psp_streamer.server
