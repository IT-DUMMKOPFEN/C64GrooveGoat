/* faac configuration for C64 GrooveGoat */
#define PACKAGE "faac"
#define PACKAGE_VERSION "2.2.0"
#define HAVE_GETOPT_H 0
#if defined(__x86_64__) || defined(__i386__)
#define HAVE_IMMINTRIN_H 1
#define HAVE_SSE2 1
#else
/* faac checks HAVE_SSE2 with #if defined(), so it must stay undefined */
#endif
#define WORDS_BIGENDIAN 0
#define FAAC_SBR_DECIMATION 1
/* FAAC_STATS must stay undefined (checked with #ifdef) */
#define MAX_CHANNELS 2
