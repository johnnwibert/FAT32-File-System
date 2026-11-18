#ifndef FAT32_H
#define FAT32_H
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    FILE *file;
    uint32_t root, sector_bytes, cluster_sectors, data_clusters;
    uint64_t fat_entries, fat_offset, data_offset, image_bytes;
} Volume;

typedef struct {
    char name[13];
    uint32_t cluster;
    bool directory;
} Entry;

bool read_volume(Volume *v);
void print_info(const Volume *v);
/* Returns 1 for success, 0 for a missing name, -1 for invalid data or I/O. 
   A NULL target lists the directory instead of looking up one entry. */
int scan_directory(const Volume *v, uint32_t start, const char *target, Entry *match);

#endif
