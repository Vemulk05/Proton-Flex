#include "ff.h"

// Simple fixed FAT timestamp: 2026-01-01 00:00:00
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25)  // year
         | ((DWORD)1 << 21)              // month
         | ((DWORD)1 << 16)              // day
         | ((DWORD)0 << 11)              // hour
         | ((DWORD)0 << 5)               // minute
         | ((DWORD)0 >> 1);              // second/2
}