#ifndef m3_config_platforms_h
#define m3_config_platforms_h

# if defined(__clang__)
#  define M3_COMPILER_CLANG 1
# elif defined(__GNUC__) || defined(__GNUG__)
#  define M3_COMPILER_GCC 1
# elif defined(_MSC_VER)
#  define M3_COMPILER_MSVC 1
# else
#  warning "Compiler not detected"
# endif

# if defined(PARTICLE)
#  define d_m3LogOutput                         false
#  define d_m3MaxFunctionStackHeight            256
# elif defined(ESP8266)
#  define d_m3LogOutput                         false
#  define d_m3MaxFunctionStackHeight            256
#  define d_m3FixedHeap                         (8*1024)
# elif defined(ESP32)
#  define d_m3MaxFunctionStackHeight            256
# elif defined(WM_W600)
#  define d_m3MaxFunctionStackHeight            256
# elif defined(BLUE_PILL)
#  define d_m3LogOutput                         false
#  define d_m3MaxFunctionStackHeight            256
#  define d_m3FixedHeap                         (8*1024)
# elif defined(FOMU)
#  define d_m3LogOutput                         false
#  define d_m3MaxFunctionStackHeight            256
#  define d_m3FixedHeap                         (8*1024)
# endif

#endif
