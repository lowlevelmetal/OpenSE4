// stb_vorbis's implementation (public domain), alone in its file: its
// macros reach nothing else, and its warnings are not ours (src/CMakeLists.txt).
// assets/sound.cpp holds what OpenSE4 does with it.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>
