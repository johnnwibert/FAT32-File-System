#ifndef FSUTIL_H
#define FSUTIL_H

#include "fat32.h"

#define FAT_FREE 0x00000000
#define FAT_EOC  0x0FFFFFFF 

#define ATTR_LONG_NAME 0x0F
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20

//one 32 byte directory entry and where it is in the image
typedef struct {
    unsigned char raw[32];
    uint64_t pos;
} DirEntry;

bool read_bytes(const Volume *v, uint64_t offset, void *buf, size_t size);
bool write_bytes(const Volume *v, uint64_t offset, const void *buf, size_t size);

uint32_t cluster_size(const Volume *v);
uint64_t cluster_offset(const Volume *v, uint32_t cluster);
uint32_t get_fat(const Volume *v, uint32_t cluster);
bool set_fat(const Volume *v, uint32_t cluster, uint32_t value);
bool is_end_of_chain(uint32_t value);
uint32_t alloc_cluster(const Volume *v);
bool zero_cluster(const Volume *v, uint32_t cluster);

bool make_short_name(const char *name, char out[11]);
uint32_t entry_cluster(const DirEntry *e);
uint32_t entry_size(const DirEntry *e);
bool entry_is_dir(const DirEntry *e);
int find_entry(const Volume *v, uint32_t dir_cluster, const char *name, DirEntry *out);
bool find_free_slot(const Volume *v, uint32_t dir_cluster, uint64_t *pos);
bool add_entry(const Volume *v, uint32_t dir_cluster, const char *name,
               unsigned char attr, uint32_t first_cluster);

#endif
