#ifndef FAT32_H
#define FAT32_H
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    FILE *file;
    uint32_t root, sector_bytes, cluster_sectors, data_clusters;
    uint64_t fat_entries, fat_offset, data_offset, image_bytes;
    uint32_t num_fats;      //how many copies of the FAT there are
    uint64_t first_fat;     //byte offset of the first FAT cpy
    uint64_t fat_bytes;     //size of one FAT copy in bytes 
} Volume;

typedef struct {
    char name[13];
    uint32_t cluster;
    bool directory;
} Entry;

bool read_volume(Volume *v);
void print_info(const Volume *v);
//eturns one for success 0 for a missing name negative one for invalid data or IO. 
int scan_directory(const Volume *v, uint32_t start, const char *target, Entry *match);

#endif
