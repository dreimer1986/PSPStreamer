# PSP Streamer

PSP Streamer macht eine lokale oder über DynDNS erreichbare Videosammlung auf einer PSP-2000/3000 mit Custom Firmware nutzbar. Der Python-Server durchsucht freigegebene Ordner und transkodiert mit FFmpeg. Die native PSP-App empfängt einen kleinen H.264-Baseline-Videostream und einen separaten MP3-Audiostream; die PSP dekodiert beides lokal.

Der erprobte Zielpfad ist 480×272, H.264 Baseline bei 20 fps und 44,1-kHz-MP3. Eingebrannte Textuntertitel, Audiospurwahl, ein wach gehaltener Bildschirm sowie große Verzeichnislisten werden unterstützt.

## Voraussetzungen

- Python 3.11 oder neuer
- FFmpeg mit `libx264`, `libmp3lame` und `libass`
- Für Untertitel: Fontconfig und DejaVu-Fonts (im Docker-Image enthalten)
- Eine PSP mit funktionierendem Infrastruktur-WLAN und CFW für die Homebrew-App

## Server direkt starten

`MEDIA_ROOTS` ist eine durch Doppelpunkte getrennte Liste erlaubter Medienwurzeln. Der Server folgt keinen Pfaden außerhalb dieser Wurzeln.

```bash
MEDIA_ROOTS='/srv/media/Serien:/srv/media/Filme' PORT=8091 \
  python3 -m psp_streamer.server
```

Danach ist die Testoberfläche unter `http://SERVER:8091` verfügbar. Sie eignet sich zum Durchsuchen und Prüfen der Transkodierung; die stabile Wiedergabe erfolgt in der PSP-App.

| Variable | Standard | Zweck |
| --- | --- | --- |
| `MEDIA_ROOTS` | `/media` | Erlaubte Videoordner, durch `:` getrennt |
| `PORT` | `8091` | HTTP-Port |
| `MAX_TRANSCODES` | `2` | Gleichzeitige FFmpeg-Prozesse; eine PSP-Wiedergabe benötigt zwei |
| `FFMPEG_PRESET` | `veryfast` | x264-Preset |
| `FFMPEG_FONTS_DIR` | DejaVu-Systemordner | Font-Ordner für eingebrannte Textuntertitel |

## Docker

Der SMB-/NFS-Mount gehört auf den Docker-Host. Er wird ausschließlich lesbar eingebunden.

```bash
MEDIA_ROOT_PATH=/srv/media PSP_STREAMER_PORT=8091 docker compose up -d --build
```

Keine SMB-Zugangsdaten in `compose.yaml` eintragen.

## PSP installieren und konfigurieren

Die Release-Datei liegt nach einem Build unter `psp-client/release/PSPStreamer/EBOOT.PBP`. Sie und `cooleyesBridge.prx` kommen nach:

```
ms0:/PSP/GAME/PSPStreamer/
```

Beim ersten Speichern einer Wiedergabeoption legt die App diese Datei an:

```
ms0:/PSP/SYSTEM/PSPStreamer.cfg
```

Der Server ist ohne Neuübersetzen konfigurierbar. Die Datei kann am PC editiert werden:

```ini
server=streamer.example.net
port=8091
audio=0
subtitle=-1
quality=2
```

`server` akzeptiert eine IPv4-Adresse oder einen DNS-/DynDNS-Namen; `http://` darf ebenfalls vorangestellt werden. Die PSP löst den Namen bei jeder neuen Verbindung auf. `quality` bedeutet `0=96k`, `1=128k`, `2=160k` MP3.

Steuerung: Kreuz öffnet Ordner bzw. Wiedergabeoptionen, Kreis kehrt aus dem Optionenmenü zurück, Links geht in den Elternordner, L/R blättert seitenweise, Quadrat lädt neu und Start beendet die App.

## Untertitel und Grenzen

ASS/SSA, SRT und andere textbasierte Untertitel werden mit libass eingebrannt. Bildbasierte PGS/VobSub/DVB-Untertitel benötigen einen anderen FFmpeg-Overlay-Pfad und sind noch nicht implementiert. Ein aktivierter Untertitel kann beim ersten Start etwas länger benötigen, weil FFmpeg Schriftarten vorbereitet.

Video-Out und eine eigene 480p-Ausgabe sind bewusst noch nicht Teil dieses Stands.

## PSP-Client bauen

Ein PSP-SDK sowie eine für PSP gebaute OpenH264-Bibliothek werden benötigt. Der Bibliothekspfad ist absichtlich kein persönlicher Hardcode:

```bash
cd psp-client
make OPENH264_DIR=/pfad/zu/openh264
cp EBOOT.PBP release/PSPStreamer/EBOOT.PBP
```

Die Bibliothek muss `libopenh264_dec_psp.a` und die zugehörigen Header bereitstellen.

## Tests

```bash
python3 -m unittest discover -s tests -v
```

## Sicherheit

Der Server hat keine Anmeldung. Nicht direkt ins öffentliche Internet stellen; DynDNS-Zugriff sollte über VPN, eine vertrauenswürdige Firewall-Regel oder ein separates Heimnetz erfolgen.
