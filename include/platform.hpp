// Platform detection macros for cross-platform support
#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #define STOW_WINDOWS 1
    #define STOW_POSIX 0
#else
    #define STOW_WINDOWS 0
    #define STOW_POSIX 1
#endif
