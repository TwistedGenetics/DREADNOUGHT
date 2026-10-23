# Recovery — read this before you flash anything

Installing custom firmware on the FM-1 is at your own risk. The FM-1 has **one** copy of its firmware
in a 1 MB flash. Under it sits the chip's own mask-ROM update mode, which no firmware can break, and the
community has a tested tool that rewrites stock firmware through it over plain USB. These are the ways
back, from easiest to last resort.

## Before every install

1. **Back up**, with the tool that belongs to the firmware currently on the box:
   - Stock firmware or FM1 Open → M-VAVE Workbench (open.m-vave.com/fm1-workbench): export projects,
     DX7 voice groups A–D (one at a time) and the sample bank separately. Project files do not contain sample audio.
   - SLOOP, Felucca, Clipper or DREADNOUGHT → their web editor's backup.
2. Charge the battery and use a USB cable that carries data, plugged straight into the computer (no hub).
3. Close every other app and browser tab that uses MIDI (Ableton, other editors, M-UPGRADE).
4. Keep the computer awake and the tab open until it says Done.

Firmware formats are not interchangeable. Projects and samples from one firmware do not open in another.

## If something goes wrong

| What you see | What to do |
|---|---|
| The install stopped part-way (cable pulled, tab closed) | The FM-1 waits in update mode. Plug back in, open the flasher, press **Install** again: it resumes and finishes. |
| DREADNOUGHT no longer starts | Hold the rescue key while switching on to enter the update loader, then install again. Until DREADNOUGHT ships its own build, this is the base firmware's: **SLOOP USB RESCUE = hold OCT− alone at power-on**. |
| You want the official firmware back | Use **M-VAVE's own tools**: the Workbench's return to V15, or the M-UPGRADE updater with the official FM-1 V15 file from m-vave.com. Our flasher links there instead of writing stock firmware itself, because SLOOP removed its in-browser return after it proved unreliable on some systems. |
| Nothing works, the FM-1 won't start or answer USB | **Chip-level rescue, no extra hardware**: X0X's `tools/fm1_rescue.py` (github.com/charlesvestal/fm1-x0x) talks to the chip's mask-ROM update mode, which shows up on USB as `WL80UBOOT1.00` (`4C4A:8057`). It backs up the whole flash first, checks the chip key and flash ID, writes back only the firmware sectors that differ from stock V15 (never the bootloader), and verifies. Carnac OS's author used it on 15 Oct 2026 to recover a build that crashed every 31 s. Needs the official `FM-1.fwsc` V15; on a Mac run it with `sudo`, on Windows install the WinUSB driver for `4C4A:8057` with Zadig first (see Carnac's `docs/fm1-recovery.md`). |
| How to reach that mode | A firmware that crashes at start-up has been seen to fall into it after two failed boots. X0X and Carnac OS also enter it when you **hold OCT− and OCT+ together for 5 s**. DREADNOUGHT will do the same, checked early in its main loop so it still works on a broken build. |
| Last resort | The **FM-1-transporter** hardware (github.com/kurogedelic/FM-1-transporter), whose protocol the rescue tool uses. |

## For DREADNOUGHT developers

- Never flash a build whose host tests or package checks failed in CI.
- Every package is checked twice: SHA-256 in the browser before writing, CRC by the loader before starting.
- The flasher only writes through a DREADNOUGHT/Felucca-family loader (`ota-FM-1_9XY`). If the box is in
  another firmware's update mode, finish that install with that firmware's own tool first.
- Changes to the update loader, the package identity scheme or the flash layout need a recovery test with
  `fm1_rescue.py` ready on the computer first.
- DREADNOUGHT checks for OCT− + OCT+ held 5 s (enter the chip's update mode) before anything else in the
  main loop can fault.
- CI will run a pre-flight check on every image before packaging it, as Carnac OS does (`fm1_preflight.py`):
  simulate the start-up copy and zeroing and refuse a package whose RAM sections don't come out as expected.
  That is exactly the bug that bricked a Carnac build.
