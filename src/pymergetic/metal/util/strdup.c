#include "pymergetic/kernel/util/strdup.h"

#include <stdlib.h>
#include <string.h>

char *pm_metal_util_strdup(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}
