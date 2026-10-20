#pragma once

// Export macro for the Hydra-discoverable classes.

#if defined(_WIN32)
#  if defined(HDPHANTASOS_EXPORTS)
#    define HDPHANTASOS_API __declspec(dllexport)
#  else
#    define HDPHANTASOS_API __declspec(dllimport)
#  endif
#else
#  define HDPHANTASOS_API __attribute__((visibility("default")))
#endif
