#pragma once
#if defined(_MSC_VER)
#include <stddef.h>
#include <stdint.h>
inline void* sbrk(intptr_t) { return (void*)0x20000; }
#endif
