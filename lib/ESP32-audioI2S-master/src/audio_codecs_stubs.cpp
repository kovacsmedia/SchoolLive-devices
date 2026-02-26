// lib/ESP32-audioI2S-master/src/audio_codecs_stubs.cpp
// Minimal C++ stubs for removed codecs (AAC/FLAC/OPUS/VORBIS)

#include <stdint.h>

// ---------------- AAC ----------------
bool   AACDecoder_IsInit() { return false; }
void   AACDecoder_AllocateBuffers() {}
void   AACDecoder_FreeBuffers() {}

int    AACGetFormat() { return 0; }
int    AACGetID() { return 0; }
int    AACGetProfile() { return 0; }

int    AACFindSyncWord(unsigned char*, int) { return -1; }
void   AACSetRawBlockParams(int, int, int, int) {}

int    AACGetBitrate() { return 0; }
int    AACGetChannels() { return 0; }
int    AACGetSampRate() { return 0; }
int    AACGetBitsPerSample() { return 0; }

int    AACDecode(unsigned char*, int*, short*) { return 0; }
int    AACGetOutputSamps() { return 0; }


// ---------------- FLAC ----------------
void   FLACDecoder_AllocateBuffers() {}
void   FLACDecoder_FreeBuffers() {}
void   FLACDecoderReset() {}

void   FLACSetRawBlockParams(unsigned char, unsigned int, unsigned char, unsigned int, unsigned int) {}
int    FLACFindSyncWord(unsigned char*, int) { return -1; }

int    FLACGetBitRate() { return 0; }
int    FLACGetChannels() { return 0; }
int    FLACGetSampRate() { return 0; }
int    FLACGetBitsPerSample() { return 0; }

int    FLACDecode(unsigned char*, int*, short*) { return 0; }
int    FLACGetOutputSamps() { return 0; }
const char* FLACgetStreamTitle() { return nullptr; }


// ---------------- OPUS ----------------
void   OPUSDecoder_AllocateBuffers() {}
void   OPUSDecoder_FreeBuffers() {}

int    OPUSFindSyncWord(unsigned char*, int) { return -1; }

int    OPUSGetChannels() { return 0; }
int    OPUSGetSampRate() { return 0; }
int    OPUSGetBitsPerSample() { return 0; }
int    OPUSGetBitRate() { return 0; }

int    OPUSDecode(unsigned char*, int*, short*) { return 0; }
int    OPUSGetOutputSamps() { return 0; }
const char* OPUSgetStreamTitle() { return nullptr; }


// ---------------- VORBIS ----------------
void   VORBISDecoder_AllocateBuffers() {}
void   VORBISDecoder_FreeBuffers() {}

int    VORBISFindSyncWord(unsigned char*, int) { return -1; }

int    VORBISGetChannels() { return 0; }
int    VORBISGetSampRate() { return 0; }
int    VORBISGetBitsPerSample() { return 0; }
int    VORBISGetBitRate() { return 0; }

int    VORBISDecode(unsigned char*, int*, short*) { return 0; }
int    VORBISGetOutputSamps() { return 0; }
const char* VORBISgetStreamTitle() { return nullptr; }