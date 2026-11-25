#ifndef DELETE_H
#define DELETE_H

#include "fat32.h"

void do_rm(const Volume *v, uint32_t current, const char *name);
void do_rmdir(const Volume *v, uint32_t current, const char *name);

#endif