static const HelpText help_de[HELP_PAGE_COUNT]={
    [HELP_PAGE_BROWSE]={
        .title="Etwas zum Abspielen auswählen",
        .step1_title="1. HOCH / RUNTER: Datei wählen",
        .step1_text="Gedrückt halten: weiterblättern.",
        .step2_title="2. X: Ordner oder Optionen öffnen",
        .step2_text="In den Optionen startet X die Datei.",
        .step3_title="3. SELECT: Einstellungen und Hilfe",
        .step3_text="START im Dateibrowser beendet App."
    },
    [HELP_PAGE_NAVIGATION]={
        .title="In der Bibliothek zurechtfinden",
        .step1_title="LINKS: zum übergeordneten Ordner",
        .step1_text="O öffnet dagegen Lokaler Speicher.",
        .step2_title="L / R: eine Seite zurück / weiter",
        .step2_text="Halten: seitenweise weiterblättern.",
        .step3_title="DREIECK auf einer Datei: Infos",
        .step3_text="X oder O schließt die Infoseite."
    },
    [HELP_PAGE_OPTIONS]={
        .title="Vor dem Start einer Datei",
        .step1_title="HOCH / RUNTER: Einstellung wählen",
        .step1_text="LINKS / RECHTS: ihren Wert ändern.",
        .step2_title="Ton und Untertitel: Spur wählen",
        .step2_text="Aus: keine Untertitel. []: Hilfe.",
        .step3_title="X: starten   O: zurück zu Dateien",
        .step3_text="Die Auswahl gilt auch für später."
    },
    [HELP_PAGE_QUALITY]={
        .title="Qualität und Wiedergabeart",
        .step1_title="Tonqualität: CBR oder VBR",
        .step1_text="V6 ist kleiner; V3 klingt besser.",
        .step2_title="Video: 20 oder 23,976 Bilder/s",
        .step2_text="23,976: flüssiger, mehr Rechenlast.",
        .step3_title="Wiedergabe: Streaming oder Download",
        .step3_text="Erst laden: später offline schauen."
    },
    [HELP_PAGE_VIDEO]={
        .title="Video: Pause, Springen und Stopp",
        .step1_title="Vollbild / TV: SELECT drücken",
        .step1_text="Öffnet die Steuerung, keine Pause.",
        .step2_title="LINKS / RECHTS wählen, dann X",
        .step2_text="Pause, Sprung, vorige/nächste Datei.",
        .step3_title="O: Leiste schließen   START: Stopp",
        .step3_text="Stopp führt zurück zum Dateibrowser."
    },
    [HELP_PAGE_VIDEO_MORE]={
        .title="Video: praktische Abkürzungen",
        .step1_title="L / R: 10 Sekunden zurück / vor",
        .step1_text="Während Wiedergabe, nicht in Pause.",
        .step2_title="HOCH / RUNTER: Lautstärke; halten!",
        .step2_text="DREIECK: LCD-Vollbild umschalten.",
        .step3_title="LCD-Fenster: SELECT pausiert",
        .step3_text="O blendet die Receiver-Regler um."
    },
    [HELP_PAGE_MUSIC]={
        .title="Musik: die wichtigsten Tasten",
        .step1_title="SELECT: Pause / weiter",
        .step1_text="Radio setzt wieder live ein.",
        .step2_title="HOCH / RUNTER: lauter / leiser",
        .step2_text="Halten ändert die Lautstärke weiter.",
        .step3_title="START: Stopp und zurück zu Dateien",
        .step3_text="Dort mit X ein anderes Lied wählen."
    },
    [HELP_PAGE_MUSIC_MORE]={
        .title="Musik: Reihenfolge und Download",
        .step1_title="Vor dem Start: Reihenfolge wählen",
        .step1_text="Der Reihe nach oder Zufall im Ordner.",
        .step2_title="Am Liedende startet das nächste",
        .step2_text="Manuelles Stoppen beendet die Reihe.",
        .step3_title="Wiedergabe: Erst laden, abspielen",
        .step3_text="Musik wird MP3; Radio bleibt live."
    },
    [HELP_PAGE_VISUALS]={
        .title="Musik: Visualisierungen",
        .step1_title="QUADRAT: Spektrum / MilkDrop",
        .step1_text="Beide reagieren auf die Musik.",
        .step2_title="DREIECK: Vollbild ein / aus",
        .step2_text="X + DREIECK geht weiterhin auch.",
        .step3_title="O: Liste der MilkDrop-Presets",
        .step3_text="Die Musik läuft beim Auswählen weiter."
    },
    [HELP_PAGE_PRESETS]={
        .title="In der Preset-Auswahl",
        .step1_title="HOCH / RUNTER: Wahl   L / R: Seite",
        .step1_text="X übernimmt das Preset; O zurück.",
        .step2_title="QUADRAT: automatischer Wechsel",
        .step2_text="Aus, Reihe, Zufall oder bewertet.",
        .step3_title="DREIECK: Wechselintervall ändern",
        .step3_text="30, 60 oder 120 Sekunden."
    },
    [HELP_PAGE_DOWNLOAD]={
        .title="Direkt auf die PSP herunterladen",
        .step1_title="1. Musik- oder Videodatei wählen",
        .step1_text="X öffnet die Wiedergabe-Optionen.",
        .step2_title="2. Wiedergabe: Erst laden wählen",
        .step2_text="Qualität/Spuren einstellen, dann X.",
        .step3_title="3. Umwandlung und Download abwarten",
        .step3_text="O bricht ab; neue Wahl setzt fort."
    },
    [HELP_PAGE_QUEUE]={
        .title="Aufträge von der Website laden",
        .step1_title="Im Browser: O zu Lokaler Speicher",
        .step1_text="QUADRAT öffnet Server-Warteschlange.",
        .step2_title="X: gewählten Auftrag herunterladen",
        .step2_text="R: alle ladbaren Aufträge der Liste.",
        .step3_title="QUADRAT: zurück zu lokalen Dateien",
        .step3_text="Zweimal wechseln lädt die Liste neu."
    },
    [HELP_PAGE_LOCAL]={
        .title="Heruntergeladene Dateien nutzen",
        .step1_title="Lokaler Speicher: X spielt ab",
        .step1_text="Ohne Server und ohne WLAN nutzbar.",
        .step2_title="DREIECK: lokalen Download löschen",
        .step2_text="X bestätigt; O behält die Datei.",
        .step3_title="PC: Memory-Stick-ZIP entpacken",
        .step3_text="Ganzen PSP-Ordner auf Karte kopieren."
    },
    [HELP_PAGE_SETTINGS]={
        .title="Einstellungen ohne Computer",
        .step1_title="HOCH/RUNTER: Zeile; LINKS/RECHTS: Wert",
        .step1_text="X öffnet die Tastatur für den Wert.",
        .step2_title="START: alle Änderungen speichern",
        .step2_text="O verlässt das Menü ohne Speichern.",
        .step3_title="Hilfe steht an erster Stelle",
        .step3_text="Beim Lesen bleibt der Entwurf da."
    },
    [HELP_PAGE_KEYBOARD]={
        .title="Text auf der PSP eingeben",
        .step1_title="Richtungstasten: Zeichen wählen",
        .step1_text="X fügt es ein. Auch Umlaute sind da.",
        .step2_title="L: letztes löschen   R: Text leeren",
        .step2_text="START übernimmt; O bricht Eingabe ab.",
        .step3_title="Zurück im Menü: START speichert",
        .step3_text="Text übernehmen allein reicht nicht."
    },
    [HELP_PAGE_TV]={
        .title="Am Fernseher abspielen",
        .step1_title="Wiedergabe stoppen; Kabel verbinden",
        .step1_text="Bibliothek: SELECT für Einstellungen.",
        .step2_title="Einmal RUNTER: Menü auf TV / LCD",
        .step2_text="X schaltet sofort um. O geht zurück.",
        .step3_title="TV-Startvorgabe bleibt unverändert",
        .step3_text="Video richtet sich weiter nach Kabel."
    },
    [HELP_PAGE_TV_MORE]={
        .title="TV: Startvorgabe behalten",
        .step1_title="TV-Menü beim Start bleibt gespeichert",
        .step1_text="Aus: LCD-Menü, Video folgt Kabel.",
        .step2_title="L beim Start halten: LCD-Menü",
        .step2_text="Später ohne App-Neustart wechseln.",
        .step3_title="Lokales Video: LCD/TV muss passen",
        .step3_text="Erst stoppen, dann Ausgang wechseln."
    },
    [HELP_PAGE_NETWORK]={
        .title="Wenn es einmal länger dauert",
        .step1_title="Bibliothek: QUADRAT lädt erneut",
        .step1_text="L + QUADRAT baut WLAN ganz neu auf.",
        .step2_title="Untertitel laden: Sekunden beachten",
        .step2_text="Das erste Auslesen kann dauern.",
        .step3_title="O bricht Laden oder Download ab",
        .step3_text="Vor Wiederholung Aufräumen abwarten."
    }
};
