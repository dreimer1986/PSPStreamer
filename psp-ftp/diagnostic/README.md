# FTP cold-boot isolation: load-only probe

This is **not an FTP release or fix**. Do not ship it as the FTP plugin.
The 0.6 staged loader still shut down the reported PSP before the delayed
service load; the mounted card has the correct binaries and no loader.log.
The old service status is not evidence of a successful new boot.

Build with `make -C psp-ftp/diagnostic` in the normal PSP toolchain environment.
The probe exports the normal lifecycle entrypoints under the same module name,
but both return success immediately. There are no runtime imports, threads,
buffers, hooks, filesystem accesses or USB operations. No log is expected.

Back up the installed `PSPStreamFTP.prx` outside the card, then replace only
that file with this probe. Keep PLUGINS.TXT, INI, the service PRX and every
other plugin unchanged. Cold boot and observe for at least 40 seconds.

- Boots: the failure requires something absent from the probe (imports,
  initialization/thread execution, or larger memory footprint). This alone
  does not identify which. Next compare an inert loader retaining its imports.
- Still shuts down: FTP service execution is not necessary to reproduce it.
  Investigate plugin registration/loading, interactions and card integrity.

FTP will deliberately be unavailable throughout this test. Restore the backed
up loader to undo the probe, or disable the FTP entry to use the console while
the real fault remains unresolved. Do not change the firmware for this test.
