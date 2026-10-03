static const HelpText help_en[HELP_PAGE_COUNT]={
    [HELP_PAGE_SM]={
        .title="Settings: StreamMaster USB",
        .step1_title="Library: SELECT opens settings",
        .step1_text="Choose StreamMaster USB, then X.",
        .step2_title="Connect USB, then set up Wi-Fi",
        .step2_text="Tests check bridge and server access.",
        .step3_title="Network transport chooses the route",
        .step3_text="Select StreamMaster to use USB Wi-Fi."
    },
    [HELP_PAGE_SM_WIFI]={
        .title="StreamMaster: Wi-Fi profiles",
        .step1_title="Choose a slot before editing Wi-Fi",
        .step1_text="Scan, select SSID and enter password.",
        .step2_title="DHCP fills address and DNS fields",
        .step2_text="Use manual values only if required.",
        .step3_title="Save / connect applies the profile",
        .step3_text="Automatic picks an available profile."
    },
    [HELP_PAGE_BT]={
        .title="StreamMaster: Bluetooth",
        .step1_title="Open Bluetooth controller submenu",
        .step1_text="Put the controller in pairing mode.",
        .step2_title="Find controllers; X connects",
        .step2_text="TRIANGLE twice forgets the selection.",
        .step3_title="Buttons / reconnect opens mapping",
        .step3_text="Saved devices can reconnect themselves."
    },
    [HELP_PAGE_BT_LEARN]={
        .title="Controller: learn buttons and stick",
        .step1_title="Choose a PSP button, then X",
        .step1_text="Release controls; press its new button.",
        .step2_title="PS / Home needs firmware 0.3.13",
        .step2_text="Analog: circle the stick, then center.",
        .step3_title="SQUARE skips; START saves mapping",
        .step3_text="Saved separately for each controller."
    },
    [HELP_PAGE_CONSOLIZER]={
        .title="PSP Consolizer: games and USB",
        .step1_title="Mapped PS / Home opens system menu",
        .step1_text="Fallback: hold START + SELECT for 1s.",
        .step2_title="XMB: hold NOTE + Volume Down for 2s",
        .step2_text="USB RELEASED: open USB Connection.",
        .step3_title="Exit storage before the same chord",
        .step3_text="Resumes Consolizer; INI report=0: quiet."
    },
    [HELP_PAGE_PLAYLIST]={
        .title="Shared playlist",
        .step1_title="Open Playlist in the library",
        .step1_text="Add media in the server web page.",
        .step2_title="X: options, then play the list",
        .step2_text="Music and video in saved order.",
        .step3_title="TRIANGLE: move / remove / mode",
        .step3_text="Repeat / list shuffle here too."
    },
    [HELP_PAGE_TRAVEL]={
        .title="Ready for a trip?",
        .step1_title="Local storage: L opens overview",
        .step1_text="File status and free space.",
        .step2_title="Downloads: check before transfer",
        .step2_text="Remaining size after encoding.",
        .step3_title="X: confirm transfer   O: cancel",
        .step3_text="Size check, not an integrity scan."
    },
    [HELP_PAGE_COMFORT]={
        .title="Quick access and limits",
        .step1_title="SELECT, then Quick access",
        .step1_text="Favorites / history for this server.",
        .step2_title="Highlight a file or folder first",
        .step2_text="Then Add/remove selected favorite.",
        .step3_title="Limits: LEFT / RIGHT to change",
        .step3_text="Stops playback; no power-off."
    },
    [HELP_PAGE_RESUME]={
        .title="Continue a video",
        .step1_title="X: continue   []: restart   O: back",
        .step1_text="After options, if a position exists.",
        .step2_title="Positions saved after stopping",
        .step2_text="Files / DLNA too; survives updates.",
        .step3_title="Plex / Jellyfin: provider position",
        .step3_text="No prompt for autoplay / remote."
    },
    [HELP_PAGE_PROFILES]={
        .title="Server profiles",
        .step1_title="Save settings with START first",
        .step1_text="Quick access > Server profiles.",
        .step2_title="[]: name and save connection",
        .step2_text="Five slots; password on the stick.",
        .step3_title="X: use profile   TRIANGLE: delete",
        .step3_text="Switch without restarting the app."
    },
    [HELP_PAGE_BROWSE]={
        .title="Choose something to play",
        .step1_title="1. UP / DOWN: choose a file",
        .step1_text="Hold the button to keep scrolling.",
        .step2_title="2. X: open folder or options",
        .step2_text="In options, X starts playback.",
        .step3_title="3. SELECT: settings and help",
        .step3_text="START in the library exits the app."
    },
    [HELP_PAGE_NAVIGATION]={
        .title="Find your way around",
        .step1_title="LEFT: parent folder",
        .step1_text="O opens Local storage instead.",
        .step2_title="L / R: jump a page",
        .step2_text="Hold to keep turning pages.",
        .step3_title="TRIANGLE on a file: details",
        .step3_text="X or O closes the information page."
    },
    [HELP_PAGE_OPTIONS]={
        .title="Before starting a file",
        .step1_title="UP / DOWN: select a setting",
        .step1_text="LEFT / RIGHT: change its value.",
        .step2_title="Audio and subtitles: pick a track",
        .step2_text="Off means no subtitles. []: help.",
        .step3_title="X: start   O: return to files",
        .step3_text="Triangle: save/forget series tracks."
    },
    [HELP_PAGE_QUALITY]={
        .title="Quality and playback mode",
        .step1_title="Audio quality: CBR or VBR",
        .step1_text="V6 is smaller; V3 is higher quality.",
        .step2_title="Video: 20 or 23.976 fps",
        .step2_text="23.976 is smoother, but more work.",
        .step3_title="Playback: stream or download",
        .step3_text="Download first for offline use."
    },
    [HELP_PAGE_VIDEO]={
        .title="Video: pause, seek and stop",
        .step1_title="Fullscreen / TV: SELECT",
        .step1_text="Opens controls; does not pause yet.",
        .step2_title="LEFT / RIGHT, then X",
        .step2_text="Pause, seek, or previous/next file.",
        .step3_title="O: close controls   START: stop",
        .step3_text="Stop returns to the file browser."
    },
    [HELP_PAGE_VIDEO_MORE]={
        .title="Video: useful shortcuts",
        .step1_title="L / R: back / forward 10 seconds",
        .step1_text="While playing, not while paused.",
        .step2_title="UP / DOWN: volume (hold repeats)",
        .step2_text="TRIANGLE: toggle LCD fullscreen.",
        .step3_title="LCD window: SELECT pauses",
        .step3_text="O toggles receiver controls."
    },
    [HELP_PAGE_MUSIC]={
        .title="Music: the essentials",
        .step1_title="SELECT: pause / continue",
        .step1_text="Radio reconnects to the live point.",
        .step2_title="UP / DOWN: louder / quieter",
        .step2_text="Hold for gradual volume changes.",
        .step3_title="START: stop and return to files",
        .step3_text="Choose another song there with X."
    },
    [HELP_PAGE_MUSIC_MORE]={
        .title="Music: order and offline use",
        .step1_title="Before play: choose Play order",
        .step1_text="Sequential or Shuffle in the folder.",
        .step2_title="At song end: next song starts",
        .step2_text="Manual Stop ends that sequence.",
        .step3_title="Playback: Download, then play",
        .step3_text="Music becomes MP3; radio is live."
    },
    [HELP_PAGE_VISUALS]={
        .title="Music: visualizations",
        .step1_title="SQUARE: change visualization",
        .step1_text="Spectrum / MilkDrop / Cave.",
        .step2_title="TRIANGLE: fullscreen on / off",
        .step2_text="X: show track title for five seconds.",
        .step3_title="O: visualization options",
        .step3_text="Spectrum: FFT/bars. MilkDrop: presets."
    },
    [HELP_PAGE_PRESETS]={
        .title="Inside the preset list",
        .step1_title="UP / DOWN: choose   L / R: page",
        .step1_text="X applies the preset; O returns.",
        .step2_title="SQUARE: automatic preset mode",
        .step2_text="Off, sequential, random or rated.",
        .step3_title="TRIANGLE: change the interval",
        .step3_text="SELECT: live fades and cut options."
    },
    [HELP_PAGE_FLIGHT]={
        .title="Cave: take the controls",
        .step1_title="L + R: open the flight menu",
        .step1_text="LEFT/RIGHT: ship. Stick: steer. X: fire.",
        .step2_title="L / R: roll   UP / DOWN: speed",
        .step2_text="Double L / R: roll blocks projectiles.",
        .step3_title="Hold L + R for 5 seconds to exit",
        .step3_text="Fullscreen while flying; START stops."
    },
    [HELP_PAGE_FLIGHT_GAME]={
        .title="Flight: shield and Hall of Fame",
        .step1_title="Menu: Game Start / Hall of Fame / Exit",
        .step1_text="UP/DOWN selects, X confirms, O exits.",
        .step2_title="Wall hit: -20 shield; 1s protection",
        .step2_text="Shot: -10; kill: +100; second: +1 point.",
        .step3_title="Game Over: explosion, then scores",
        .step3_text="After 5s: top ten. X / O returns."
    },
    [HELP_PAGE_DOWNLOAD]={
        .title="Download straight from the PSP",
        .step1_title="1. Choose a music or video file",
        .step1_text="X opens its playback options.",
        .step2_title="2. Playback: Download, then play",
        .step2_text="Set quality/tracks, then press X.",
        .step3_title="3. Wait for conversion and transfer",
        .step3_text="O cancels; choose again to resume."
    },
    [HELP_PAGE_QUEUE]={
        .title="Download jobs from the website",
        .step1_title="Browser: O opens Local storage",
        .step1_text="SQUARE switches to Server queue.",
        .step2_title="X: download selected job",
        .step2_text="R: all listed queued/ready jobs.",
        .step3_title="SQUARE: switch back to local files",
        .step3_text="Switch twice to refresh the queue."
    },
    [HELP_PAGE_LOCAL]={
        .title="Use your downloaded files",
        .step1_title="Local storage: X plays the file",
        .step1_text="START exits app; O returns to browser.",
        .step2_title="TRIANGLE: delete local download",
        .step2_text="Confirm with X; O keeps the file.",
        .step3_title="PC: extract the Memory Stick ZIP",
        .step3_text="Copy its whole PSP folder to card."
    },
    [HELP_PAGE_SETTINGS]={
        .title="Settings without a computer",
        .step1_title="UP / DOWN: row; LEFT / RIGHT: value",
        .step1_text="X opens input or a submenu.",
        .step2_title="START: save all changes",
        .step2_text="O leaves without saving changes.",
        .step3_title="Help is the first entry",
        .step3_text="Reading help keeps your draft."
    },
    [HELP_PAGE_KEYBOARD]={
        .title="Typing on the PSP",
        .step1_title="Direction buttons: choose a symbol",
        .step1_text="X adds it. Umlauts are included.",
        .step2_title="L: delete last   R: clear text",
        .step2_text="START accepts; O cancels typing.",
        .step3_title="Back in settings: START saves",
        .step3_text="Accepting text alone does not save."
    },
    [HELP_PAGE_PLUGIN]={
        .title="Plugins: OC / Consolizer / Fullscreen",
        .step1_title="Settings: Plugins, then X",
        .step1_text="UP/DOWN: row; LEFT/RIGHT: value.",
        .step2_title="START saves the plugin INI; O cancels",
        .step2_text="Changes apply at the next app start.",
        .step3_title="Rules: SQUARE inherits global values",
        .step3_text="Old INI is kept as .ini.bak."
    },
    [HELP_PAGE_PLUGIN_RULES]={
        .title="Plugin rules and path filters",
        .step1_title="Add title ID or complete launch path",
        .step1_text="Title ID wins; first matching rule wins.",
        .step2_title="X edits; SQUARE renames a rule",
        .step2_text="TRIANGLE twice deletes; START saves.",
        .step3_title="O leaves rule values; O again cancels",
        .step3_text="Filters: case-sensitive path fragments."
    },
    [HELP_PAGE_HEALTH_RUMBLE]={
        .title="PSP game health rumble",
        .step1_title="Plugins: PSPConsolizer > Game rumble",
        .step1_text="Import code/result or add known address.",
        .step2_title="Set type and valid health range",
        .step2_text="Pointer and battle flag are optional.",
        .step3_title="Enable; O to list; START saves",
        .step3_text="Restart game. Details: RUMBLE.md."
    },
    [HELP_PAGE_PRESET_SETTINGS]={
        .title="Preset folders in settings",
        .step1_title="Choose MilkDrop preset, then X",
        .step1_text="X enters a folder; .. goes up.",
        .step2_title="X picks a file; O cancels selection",
        .step2_text="Back in settings: START saves it.",
        .step3_title="Automatic mode uses that folder",
        .step3_text="It also uses its local playlist.txt."
    },
    [HELP_PAGE_POWER]={
        .title="CPU profiles (optional plugin)",
        .step1_title="Spectrum / MilkDrop / video / menus",
        .step1_text="Separate values; OFF uses plugin INI.",
        .step2_title="LEFT / RIGHT: 1 MHz; X: type value",
        .step2_text="66-471 MHz. START saves settings.",
        .step3_title="StreamerOC: enabled + app_control=1",
        .step3_text="Overclock cannot exceed INI target."
    },
    [HELP_PAGE_SCREEN]={
        .title="LCD power saving",
        .step1_title="LCD idle: awake / music / always",
        .step1_text="Uses the PSP's display-off timer.",
        .step2_title="A button wakes the LCD",
        .step2_text="The button may also control playback.",
        .step3_title="Standby stays blocked; TV stays on",
        .step3_text="Defaults keep clocks and LCD as before."
    },
    [HELP_PAGE_TV]={
        .title="Using a television",
        .step1_title="Stop playback; connect TV cable",
        .step1_text="Library: SELECT opens settings.",
        .step2_title="DOWN once: switch menu to TV / LCD",
        .step2_text="X switches now. O returns to files.",
        .step3_title="Startup TV choice stays unchanged",
        .step3_text="Video still follows the cable."
    },
    [HELP_PAGE_TV_MORE]={
        .title="TV: keep your startup choice",
        .step1_title="TV UI at start: saved preference",
        .step1_text="Off: LCD menu, video follows cable.",
        .step2_title="Hold L at startup: keep LCD menu",
        .step2_text="Switch later without restarting.",
        .step3_title="Offline video: matching LCD/TV file",
        .step3_text="Stop first; no mid-video switching."
    },
    [HELP_PAGE_REMOTE]={
        .title="Browser: PSP controller",
        .step1_title="Server: Remote control",
        .step1_text="Open PSP buttons and text input.",
        .step2_title="Click buttons or hold to repeat",
        .step2_text="Touch or Hold L/R makes combinations.",
        .step3_title="Only controls this app, not XMB",
        .step3_text="Lost connections release all keys."
    },
    [HELP_PAGE_REMOTE_TEXT]={
        .title="Browser: type on your phone",
        .step1_title="Open a PSP text field with X",
        .step1_text="The browser enables its text box.",
        .step2_title="Send replaces the field contents",
        .step2_text="UTF-8 works; passwords stay masked.",
        .step3_title="Check on PSP, then START accepts",
        .step3_text="O cancels; settings need START again."
    },
    [HELP_PAGE_NETWORK]={
        .title="When something takes a while",
        .step1_title="Library: SQUARE retries / reloads",
        .step1_text="L + SQUARE forces a Wi-Fi rejoin.",
        .step2_title="Preparing subtitles: watch seconds",
        .step2_text="The first extraction can be slow.",
        .step3_title="O cancels loading or downloading",
        .step3_text="Wait for cleanup before retrying."
    }
};
