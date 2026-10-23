# Changelog

## Phase 0 — groundwork (2026-10-23)

- Platform investigation: `docs/PLATFORM.md` (chip, audio, controls, memory, storage, update protocol).
- Recovery guide: `docs/RECOVERY.md`.
- Feasibility report, base recommendation, revised phases and on-device UI mock: `docs/FEASIBILITY.md`, `docs/ui-mock.png`.
- GitHub Actions: firmware build of the pinned base (SLOOP 2.8.1) on Linux, flasher tests, tag releases; Pages deploy.
- Web flasher: device detection, backup gate, release list or local file, SHA-256 and identity checks,
  install with progress, resume after a cut-off, links to M-VAVE's tools for going back.

Tested: flasher unit tests (6 pass) and a Chromium end-to-end run against a simulated FM-1 (full
install, cut-off and resume). Workflows lint clean with actionlint.
The first CI run built and packaged the base firmware on GitHub; the base's own host-test suite
reported a failure there (non-blocking for now, to be read in Phase 1). Not yet tested: the flasher on a real FM-1.

### Phase 0 update — Carnac OS and X0X findings (2026-10-23)

- Clock corrected to 360 MHz (measured by X0X); single-precision float is usable in audio code.
- Measured CPU figures from Carnac OS: 8 wavetable voices ≈ 38 % of both cores.
- Wavetable plan now uses Carnac's proven format (512-sample frames, 7 mip levels, read from flash).
- Recovery: chip-level rescue over plain USB with X0X's `fm1_rescue.py`; OCT− + OCT+ rescue entry and a CI image pre-flight check added to Phase 1.

## Phase 1 — first DREADNOUGHT build (2026-10-23)

- Forked SLOOP 2.8.1 into the repo (credits and what changed: `docs/UPSTREAM.md`); CI now builds this tree.
- Renamed to DREADNOUGHT: boot splash (wordmark over a Reese trace), About page, USB port name, rescue and crash
  screens. Dev builds report `FM-1_900`; tagged releases `FM-1_9XY`.
- Safety: OCT− + OCT+ held 5 s on the rescue screen now enters the chip's own update mode too; every CI build runs
  a start-up pre-flight check (`tools/preflight.py`, from Carnac OS) and fails on a bad image.
- New engine: **REESE** (engine 12). Up to 7 drifting saws, Moog-style ladder with drive, a swept comb for the
  hollow movement, a clean sine on the fundamental. Eight presets at the top of the BASS list: CLASSIC RS,
  HOLLOW RS, STACK RS, NEURO RS, DARK RS, MOVING RS, SUB REESE, and HOOVER RS under LEAD.
- Deferred to Phase 2: Clipper's full HYBRID voice. It's woven into Clipper's own voice, editor and UI layers,
  so it comes in with the Massive-style voice and pages rather than half-ported now.

Tested on the host: every inherited host test, golden renders for the 8 new presets, health (no clipping, DC
or hanging voices), UI fuzz, recovery hold. CI: the firmware builds, packages (610 KB) and passes the
pre-flight check. Not yet tested: anything on a real FM-1.
