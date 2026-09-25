static const HelpText help_en[HELP_PAGE_COUNT]={
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
        .step3_text="Your choices are saved for later."
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
        .step3_text="Cave: effects. MilkDrop: presets."
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
        .step1_title="L + R together: flight on / off",
        .step1_text="Analog stick steers into branches.",
        .step2_title="L / R: roll   UP / DOWN: speed",
        .step2_text="Forward only; walls block the ship.",
        .step3_title="TRIANGLE: fullscreen   START: stop",
        .step3_text="Invert Y in Cave options if wanted."
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
        .title="StreamerOC plugin settings",
        .step1_title="Settings: StreamerOC plugin, then X",
        .step1_text="UP/DOWN: row; LEFT/RIGHT: value.",
        .step2_title="START saves the plugin INI; O cancels",
        .step2_text="Changes apply at the next app start.",
        .step3_title="Use only clocks tested on this PSP",
        .step3_text="Old INI is kept as .ini.bak."
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
