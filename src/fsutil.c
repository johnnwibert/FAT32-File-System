#include "fsutil.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

//read 2 or 4 bytes stored little endian
static uint32_t get16(const unsigned char *p) {
    return p[0] | (p[1] << 8);
}

static uint32_t get32(const unsigned char *p) {
    return get16(p) | (get16(p + 2) << 16);
}

//write 2 or 4 bytes little endian
static void put16(unsigned char *p, uint32_t value) {
    p[0] = value & 0xFF;
    p[1] = (value >> 8) & 0xFF;
}

static void put32(unsigned char *p, uint32_t value) {
    put16(p, value & 0xFFFF);
    put16(p + 2, value >> 16);
}

bool read_bytes(const Volume *v, uint64_t offset, void *buf, size_t size) {
    fseek(v->file, (long)offset, SEEK_SET);
    return fread(buf, 1, size, v->file) == size;
}

bool write_bytes(const Volume *v, uint64_t offset, const void *buf, size_t size) {
    fseek(v->file, (long)offset, SEEK_SET);
    bool ok = fwrite(buf, 1, size, v->file) == size;
    fflush(v->file);
    return ok;
}

uint32_t cluster_size(const Volume *v) {
    return v->sector_bytes * v->cluster_sectors;
}

//byte offset where a data cluster starts
uint64_t cluster_offset(const Volume *v, uint32_t cluster) {
    return v->data_offset + (uint64_t)(cluster - 2) * cluster_size(v);
}

//returns what the FAT says comes after this cluster
uint32_t get_fat(const Volume *v, uint32_t cluster) {
    unsigned char buf[4];
    if (!read_bytes(v,v->fat_offset +cluster * 4, buf, 4))
        return FAT_EOC;
    return get32(buf) & 0x0FFFFFFF;
}

//sets a FAT value in every copy of the FAT
bool set_fat(const Volume *v, uint32_t cluster, uint32_t value) {
    unsigned char buf[4];
    put32(buf, value);
    for (uint32_t i = 0; i < v->num_fats; i++) {
        uint64_t pos = v->first_fat + i * v->fat_bytes + cluster * 4;
        if (!write_bytes(v, pos, buf, 4))
            return false;
    }
    return true;
}

bool is_end_of_chain(uint32_t value) {
    return value < 2 || value >= 0x0FFFFFF8;
}

//finds a free cluster marks it as used and returns it
uint32_t alloc_cluster(const Volume *v) {
    for (uint32_t c = 2; c < v->data_clusters + 2;c++) {
        if (get_fat(v, c) == FAT_FREE) {
            set_fat(v, c, FAT_EOC);
            return c;
        }
    }
    return 0;
}

//fills a cluster with zeros
bool zero_cluster(const Volume *v, uint32_t cluster) {
    unsigned char *zeros = calloc(cluster_size(v), 1);
    if (!zeros)
        return false;
    bool ok = write_bytes(v, cluster_offset(v, cluster), zeros, cluster_size(v));
    free(zeros);
    return ok;
}

bool make_short_name(const char *name, char out[11]) {
    size_t len = strlen(name);
    memset(out, ' ', 11);

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        memcpy(out, name, len);
        return true;
    }
    if (len == 0 || len > 8)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (strchr(".\"*+,/:;<=>?[\\]|", name[i]))
            return false;
        out[i] = toupper((unsigned char)name[i]);
    }
    return true;
}

uint32_t entry_cluster(const DirEntry *e) {
    return (get16(e->raw + 20) << 16) | get16(e->raw + 26);
}

uint32_t entry_size(const DirEntry *e) {
    return get32(e->raw + 28);
}

bool entry_is_dir(const DirEntry *e) {
    return (e->raw[11] & ATTR_DIRECTORY) != 0;
}

int find_entry(const Volume *v, uint32_t dir_cluster, const char *name, DirEntry *out) {
    char want[11];
    if (!make_short_name(name, want))
        return 0;

    unsigned char *buf = malloc(cluster_size(v));
    if (!buf)
        return -1;

    int result = 0;
    uint32_t cluster = dir_cluster;
    while (!is_end_of_chain(cluster)) {
        if (!read_bytes(v, cluster_offset(v, cluster), buf, cluster_size(v))) {
            result = -1;
            break;
        }
        for (uint32_t i = 0; i < cluster_size(v); i += 32) {
            unsigned char *p = buf + i;
            if (p[0] == 0x00)
                break;      //end of directory 
            if (p[0] == 0xE5 || p[11] == ATTR_LONG_NAME)
                continue;   //deleted entry or long name piece
            if (memcmp(p, want, 11) == 0) {
                memcpy(out->raw, p, 32);
                out->pos = cluster_offset(v, cluster) + i;
                free(buf);
                return 1;
            }
        }
        cluster = get_fat(v, cluster);
    }
    free(buf);
    return result;
}

//finds an empty 32 byte slot in a directory
bool find_free_slot(const Volume *v, uint32_t dir_cluster, uint64_t *pos) {
    unsigned char entry[32];
    uint32_t cluster = dir_cluster;
    uint32_t last = dir_cluster;

    while (!is_end_of_chain(cluster)) {
        for (uint32_t i = 0; i < cluster_size(v); i += 32) {
            uint64_t spot = cluster_offset(v, cluster) + i;
            if (!read_bytes(v, spot, entry, 32))
                return false;
            if (entry[0] == 0x00 || entry[0] == 0xE5) {
                *pos = spot;
                return true;
            }
        }
        last = cluster;
        cluster = get_fat(v, cluster);
    }

    //o room so link a new empty cluster onto the directory
    uint32_t new_cluster = alloc_cluster(v);
    if (new_cluster == 0)
        return false;
    zero_cluster(v, new_cluster);
    set_fat(v, last, new_cluster);
    *pos = cluster_offset(v, new_cluster);
    return true;
}

//makes a new directory entry and writes it into the directory
bool add_entry(const Volume *v, uint32_t dir_cluster, const char *name,
               unsigned char attr, uint32_t first_cluster) {
    unsigned char raw[32] = {0};
    make_short_name(name, (char *)raw);
    raw[11] = attr;
    put16(raw + 20, first_cluster >> 16);
    put16(raw + 26, first_cluster & 0xFFFF);

    uint64_t pos;
    if (!find_free_slot(v, dir_cluster, &pos))
        return false;
    return write_bytes(v, pos, raw, 32);
}
