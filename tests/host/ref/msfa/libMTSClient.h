// stand-in for MTS-ESP (none: the FM tests)
#pragma once
struct MTSClient;
static inline bool MTS_HasMaster(MTSClient *) { return false; }
static inline double MTS_NoteToFrequency(MTSClient *, char, char) { return 440.0; }
