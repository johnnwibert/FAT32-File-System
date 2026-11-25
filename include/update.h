#ifndef UPDATE_H
#define UPDATE_H

#include "fat32.h"

void do_write(const Volume *v, uint32_t current,
              const char *name, const char *string);

void do_mv(const Volume *v, uint32_t current,
           const char *source, const char *dest);

#endif