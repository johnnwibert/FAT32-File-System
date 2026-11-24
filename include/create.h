#ifndef CREATE_H
#define CREATE_H

#include "fat32.h"

//art 3:create commands
void do_mkdir(const Volume *v, uint32_t current, const char *name);
void do_creat(const Volume *v, uint32_t current, const char *name);

#endif
