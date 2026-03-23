/*
 * stb_vorbis compilation unit for ESP32.
 *
 * Compiled as C (not C++) to avoid name-mangling issues with tsf.h
 * which includes stb_vorbis declarations via extern "C".
 *
 * STDIO disabled — all OGG decoding uses the from-memory API
 * (stb_vorbis_open_memory) which TSF calls with the SFO sample block.
 */

#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
