// lib/ESP32-audioI2S-master/src/audio_codecs_stubs.cpp
// Minimal C++ stubs for removed codecs (AAC/FLAC/OPUS/VORBIS).
//
// FONTOS: a paramétertípusoknak PONTOSAN egyezniük kell a kodek-header-ekben
// szereplő deklarációkkal (uint8_t/uint32_t és NEM unsigned char/unsigned int),
// különben a C++ name mangling miatt a linker nem találja a stubokat.
// Az ESP-IDF v5.x toolchain (xtensa-esp-elf @ 14.2.0) a uint32_t-t
// `unsigned long`-ként mangle-li (`m`), a unsigned int-tel (`j`) nem
// kompatibilis - pedig 32-bit-en mindkettő ugyanazt a 4 byte-ot foglalja.

#include <stdint.h>

// ---------------- AAC ----------------
bool    AACDecoder_AllocateBuffers(void) { return false; }
bool    AACDecoder_IsInit(void) { return false; }
void    AACDecoder_FreeBuffers(void) {}
int     AACFlushCodec() { return 0; }
int     AACFindSyncWord(uint8_t* buf, int nBytes) { (void)buf; (void)nBytes; return -1; }
int     AACSetRawBlockParams(int copyLast, int nChans, int sampRateCore, int profile) {
    (void)copyLast; (void)nChans; (void)sampRateCore; (void)profile;
    return 0;
}
int     AACDecode(uint8_t* inbuf, int* bytesLeft, short* outbuf) {
    (void)inbuf; (void)bytesLeft; (void)outbuf;
    return 0;
}
int     AACGetSampRate() { return 0; }
int     AACGetChannels() { return 0; }
int     AACGetID() { return 0; }
uint8_t AACGetProfile() { return 0; }
uint8_t AACGetFormat() { return 0; }
int     AACGetBitsPerSample() { return 0; }
int     AACGetBitrate() { return 0; }
int     AACGetOutputSamps() { return 0; }


// ---------------- FLAC ----------------
bool     FLACDecoder_AllocateBuffers(void) { return false; }
void     FLACDecoder_ClearBuffer() {}
void     FLACDecoder_FreeBuffers() {}
void     FLACDecoderReset() {}
int      FLACFindSyncWord(unsigned char* buf, int nBytes) { (void)buf; (void)nBytes; return -1; }
int      FLACparseOGG(uint8_t* inbuf, int* bytesLeft) { (void)inbuf; (void)bytesLeft; return 0; }
void     FLACSetRawBlockParams(uint8_t Chans, uint32_t SampRate, uint8_t BPS,
                               uint32_t tsis, uint32_t AuDaLength) {
    (void)Chans; (void)SampRate; (void)BPS; (void)tsis; (void)AuDaLength;
}
uint8_t  FLACGetBitsPerSample() { return 0; }
uint8_t  FLACGetChannels() { return 0; }
uint32_t FLACGetSampRate() { return 0; }
uint32_t FLACGetBitRate() { return 0; }
uint32_t FLACGetAudioFileDuration() { return 0; }
int      FLACDecode(uint8_t* inbuf, int* bytesLeft, short* outbuf) {
    (void)inbuf; (void)bytesLeft; (void)outbuf;
    return 0;
}
int      FLACGetOutputSamps() { return 0; }
char*    FLACgetStreamTitle() { return nullptr; }


// ---------------- OPUS ----------------
bool     OPUSDecoder_AllocateBuffers() { return false; }
void     OPUSDecoder_FreeBuffers() {}
void     OPUSDecoder_ClearBuffers() {}
void     OPUSsetDefaults() {}
int      OPUSDecode(uint8_t* inbuf, int* bytesLeft, short* outbuf) {
    (void)inbuf; (void)bytesLeft; (void)outbuf;
    return 0;
}
uint8_t  OPUSGetChannels() { return 0; }
uint32_t OPUSGetSampRate() { return 0; }
uint8_t  OPUSGetBitsPerSample() { return 0; }
uint32_t OPUSGetBitRate() { return 0; }
int      OPUSFindSyncWord(unsigned char* buf, int nBytes) { (void)buf; (void)nBytes; return -1; }
int      OPUSparseOGG(uint8_t* inbuf, int* bytesLeft) { (void)inbuf; (void)bytesLeft; return 0; }
int      OPUSGetOutputSamps() { return 0; }
char*    OPUSgetStreamTitle() { return nullptr; }


// ---------------- VORBIS ----------------
bool     VORBISDecoder_AllocateBuffers() { return false; }
void     VORBISDecoder_FreeBuffers() {}
void     VORBISDecoder_ClearBuffers() {}
void     VORBISsetDefaults() {}
int      VORBISDecode(uint8_t* inbuf, int* bytesLeft, short* outbuf) {
    (void)inbuf; (void)bytesLeft; (void)outbuf;
    return 0;
}
uint8_t  VORBISGetChannels() { return 0; }
uint32_t VORBISGetSampRate() { return 0; }
uint8_t  VORBISGetBitsPerSample() { return 0; }
uint32_t VORBISGetBitRate() { return 0; }
int      VORBISFindSyncWord(unsigned char* buf, int nBytes) { (void)buf; (void)nBytes; return -1; }
int      VORBISparseOGG(uint8_t* inbuf, int* bytesLeft) { (void)inbuf; (void)bytesLeft; return 0; }
int      VORBISGetOutputSamps() { return 0; }
char*    VORBISgetStreamTitle() { return nullptr; }
