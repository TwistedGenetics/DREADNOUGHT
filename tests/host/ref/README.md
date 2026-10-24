# The FM reference: Dexed's MSFA

`msfa/` is the FM engine core of [Dexed](https://github.com/asb2m10/dexed) (`source/msfa`, commit fea2f0f), unchanged:
Copyright 2012 Google Inc., Pascal Gauthier and the Dexed authors, under the Apache License 2.0 (each file's header;
Dexed keeps that folder on Apache 2.0, the rest of Dexed is GPL-3.0). It is built only for the host tests:
`tests/host/fm_test.c` renders the same voices through it and through DREADNOUGHT's port (`src/dsp/dn_fm.c`) and
compares them. It is not part of the firmware.

The few things it needs from the rest of Dexed are stood in for here: `Tunings.h` and `tuning_std.cc` (standard
tuning only), `libMTSClient.h` (no MTS-ESP), `debugger/Logger.h` (no logging). `ref_fm.cc` plays a voice the way
Dexed's `PluginProcessor` does (one LFO, blocks of 64, each voice clipped to 16 bits).
