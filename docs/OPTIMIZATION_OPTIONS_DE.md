# Erläuterung der zurückgestellten Optimierungen

Stand: 29.09.2026. Die Nummern entsprechen den Punkten 5–13 der aktuellen
Optimierungsliste. Dies ist eine Erklärung, keine Freigabe oder Zusage eines
Geschwindigkeitsgewinns. Die stabile Übertragung bleibt unverändert.

5. **Paket-Allokationspool:** Speicher für komprimierte Medienpakete wiederverwenden,
   statt laufend unterschiedlich große Blöcke anzufordern und freizugeben. Das
   könnte Verwaltungsaufwand und Fragmentierung reduzieren. Unterschiedliche
   Paketgrößen, Pufferbesitz und Abbruchpfade machen es jedoch nicht risikofrei.
   Erst sinnvoll, wenn die Speicherverwaltung tatsächlich messbar bremst.

6. **StreamMaster-Profile und FNV:** Bei gleichem Film, HTTP, Stick und Takt prüfen,
   wie große Datenblöcke und wie viele offene Anfragen gleichzeitig günstig sind.
   FNV ist die vorhandene Prüfsumme der Transportdaten; ihr Rechenweg könnte bei
   exakt gleichem Ergebnis beschleunigt werden. Größere Puffer sind nicht automatisch
   besser: internes RAM ist knapp, mehr Tiefe kann Bedienung und andere Kanäle stören.

7. **UI, Protokolle und Datenlieferung:** Unterscheiden, ob Zeit durch Anzeigen,
   Log-Schreiben, Server/SMB oder den Access Point verloren geht. Nur nachgewiesene
   unnötige Arbeit reduzieren. Insbesondere reine Debug-Kosten rechtfertigen keinen
   Umbau des normalen Players. Das ist zunächst Diagnose, kein neues Protokoll.

8. **ESP-Planung, internes RAM/IRAM und TCP – komplex:** Entscheiden, welcher
   Verarbeitungsschritt wann Rechenzeit bekommt und welche heißen Daten oder
   Funktionen im schnellen internen Speicher liegen. TCP-Puffer puffern Netzbursts,
   konkurrieren aber mit USB um Speicher. Zu aggressive Änderungen können Watchdog,
   Verbindungsaufbau oder Steuerbefehle beeinträchtigen. Kein pauschaler PSRAM-Ausbau.

9. **Weniger Kopien / kreditbasierter Empfang – komplex:** Ringkopie und Prüfsumme
   könnten denselben Durchlauf nutzen. Direkte DMA-Übergabe würde eine weitere Kopie
   sparen, verlangt aber wasserdichte Regeln, wann ein USB-Puffer wiederverwendet
   werden darf. Kreditbasierter Empfang wäre ein größerer Protokollumbau: Die PSP
   meldet freie Kapazität, der ESP liefert innerhalb dieser Grenze fortlaufend.
   Damit ließen sich Anfragepausen reduzieren. Der bisher gemessene DMA-Kopieranteil
   war dagegen sehr klein; das allein verspricht keinen großen Gewinn.

10. **PSP-Zero-Copy, andere Prüfsumme, parallele Transfers/Uploads – sehr komplex:**
    Gemeinsame Kernel-/App-Puffer könnten Kopien sparen, sind bei Cache-Kohärenz und
    Abbrüchen aber empfindlich. Eine andere Prüfsumme benötigt Protokoll-Aushandlung
    und gleichwertige Fehlererkennung. Parallele Downloads teilen sich weiterhin
    denselben USB-Weg und Stick. Eine Upload-Pipeline wäre primär eine neue Funktion
    für die Gegenrichtung, keine Beschleunigung der heutigen Downloads.

11. **HTTPS:** TLS-Daten günstiger puffern oder Sitzungen wiederverwenden. Letzteres
    hilft hauptsächlich beim erneuten Verbindungsaufbau, nicht automatisch bei einem
    langen laufenden Download. Zertifikatsprüfung bleibt erhalten. Relevant nur für
    HTTPS; HTTP wird dadurch nicht schneller.

12. **Speicherkarte / SHA-256:** Übertragung und abschließende Integritätsprüfung
    getrennt messen. Asynchrone Schreibzugriffe sind bereits vorhanden. Weitere
    Anpassungen der Blockgrößen helfen nur bei passenden Karten-/Treiberengpässen;
    schnelleres SHA-256 verkürzt vor allem den Abschluss. Das vollständige erneute
    Lesen zur Prüfung wird nicht entfernt.

13. **Compiler und Takt:** LTO optimiert über Quelldateigrenzen hinweg, PGO nutzt
    gemessene Laufzeitprofile für Compilerentscheidungen. Beides kann auch größere
    oder ungünstigere Programme erzeugen. Die PSP-App verwendet bereits O3; ein
    generelles weiteres O3-Umschalten fehlt nicht. Bewährte Taktprofile vergleichen
    wäre eine Messung, kein neuer Übertaktungsversuch.

Empfehlung: zunächst 5–7 nur bei konkretem Messbedarf; 11 bei einem bewussten
HTTPS-Schwerpunkt. 8–10 nicht ohne deutlichen Engpassnachweis. 12 und 13 sind
Feinarbeit ohne garantierten Nutzen. Details und bisherige Messgrenzen stehen in
[der USB-Übersicht](STREAMMASTER_OPTIMIZATION_INVENTORY.md).
