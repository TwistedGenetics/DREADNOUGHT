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
