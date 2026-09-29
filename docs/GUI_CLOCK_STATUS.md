# GUI status and clock profiles

- Default CPU profiles: browser 222 MHz, spectrum/music 133 MHz, MilkDrop or
  Monkey 266 MHz, video 333 MHz, download workflow and verification 333 MHz.
- Existing CFG values take precedence. `download_cpu_mhz` is saved and editable
  in Settings; zero leaves the current clock alone during the workflow.
- All changes use the existing StreamerOC cooperation API, including the
  configured overclock ceiling. There are no new direct Sony/PLL writes.
- Restore browser clock after joined download worker, including cancellation,
  verification failure and thread-start failure. Failed thread creation does
  not acquire a clock override. Main-menu exit handling is unchanged.
- Rename the visualization clock label to MilkDrop/Monkey; preserve the
  compatible `milkdrop_cpu_mhz` CFG key.
- LCD glyphs keep their dimensions and use alpha blending. The tiny network
  label also uses this compact renderer on TV without scaling the whole GUI.
- Network data is collected by the existing GUI worker, then published after
  its join. Draw routines consume cached data only. Media playback disables
  these status queries; no scan or additional server request is introduced.
- Standard builds are now O3+LTO. Plain O3 remains a separately packaged fallback.

## Verification

Built O3 and O3+LTO before testing. Seven targeted host tests passed: settings
round-trip/navigation, clock policy, remote input mailbox, TV ownership/render,
Wi-Fi source/throttle/playback suppression, LCD alpha/clipping, and download
clock lifecycle source checks. The TV harness needed existing series-preference
mock globals updated; production series behavior was not changed.

Hardware checks: inspect the small LCD/TV label and LCD text, change the download
clock to 333/400 MHz and verify return to the configured menu clock after both
completion and cancellation. No server, StreamMaster firmware or OC update is
required. Existing CFG profiles must be edited explicitly to adopt new defaults.
