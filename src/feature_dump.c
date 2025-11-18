/* feature_dump.c
   Small shim for future expansion. Currently the heavy lifting is in xeno_wrapper.c.
   This file exists so you can extend and customize feature serialization later.
*/

#include "xeno_wrapper.h"
#include <stdio.h>

void placeholder_feature_dump(void) {
    FILE *f = fopen("/storage/emulated/0/eden_wrapper/logs/feature_dump_placeholder.txt", "w");
    if (f) {
        fprintf(f, "placeholder feature dump\n");
        fclose(f);
    }
}
