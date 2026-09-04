# PSP-Client

Dieser Ordner enthält die native PSP-Anwendung: H.264-Baseline-Video über die Media Engine, MP3-Audio über den PSP-DAC, Untertitel-/Audiospurwahl und die Bridge für den AVC-Pfad.

Die Installation, Konfigurationsdatei `ms0:/PSP/SYSTEM/PSPStreamer.cfg` und der Build-Aufruf sind im [Projekt-README](../README.md) beschrieben.

Für einen lokalen Build muss `OPENH264_DIR` auf die PSP-kompilierte OpenH264-Bibliothek zeigen:

```bash
make OPENH264_DIR=/pfad/zu/openh264
```
