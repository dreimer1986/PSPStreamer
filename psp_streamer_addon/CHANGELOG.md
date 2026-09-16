# Changelog

## 0.1.28

- VA-API compatibility candidate for PSP AVC error 80628002 after the first
  TV frame: remove optional H.264 SEI metadata, retaining coded picture data,
  SPS/PPS, FLV timestamps and audio. Software and NVENC are unchanged.
- Host validation now checks that VA-API output contains no SEI. Actual PSP
  firmware compatibility still requires hardware verification.

## 0.1.27

- Optional Intel/AMD VA-API and NVIDIA NVENC video encoding; software remains default.
- Real PSP-format encoder probe, automatic-mode software fallback before media output.
- Intel/Mesa VA-API drivers and Home Assistant GPU device access.
- Encoder diagnostics in the web UI; same server features as normal Docker.
- App icon and logo.
