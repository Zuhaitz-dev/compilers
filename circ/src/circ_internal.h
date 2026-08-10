#ifndef CIRC_INTERNAL_H
#define CIRC_INTERNAL_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
static inline char *circ_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = (char *)malloc(len);
    if (p)
    {
        memcpy(p, s, len);
    }
    return p;
}
#else
#define circ_strdup strdup
#endif

#endif
