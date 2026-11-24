#include "fat32.h"
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static uint32_t le16(const unsigned char *p) {
    return p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le32(const unsigned char *p) {
    return le16(p) | (le16(p + 2) << 16);
}

static bool read_at(FILE *f, uint64_t offset, void *buf, size_t size) {
    return offset <= LONG_MAX && fseek(f, (long)offset, SEEK_SET) == 0 && fread(buf, 1, size, f) == size;
}

static bool valid_cluster(const Volume *v, uint32_t cluster) {
    return cluster >= 2 && cluster - 2 < v->data_clusters;
}

bool read_volume(Volume *v) {
    unsigned char b[512];
    if (fseek(v->file, 0, SEEK_END) != 0) return false;
    long size = ftell(v->file);
    if (size < 512 || !read_at(v->file, 0, b, sizeof b)) return false;
    v->image_bytes = (uint64_t)size;
    v->sector_bytes = le16(b + 11);
    v->cluster_sectors = b[13];
    uint32_t reserved = le16(b + 14), fats = b[16], sectors = le32(b + 32);
    uint32_t fat_sectors = le32(b + 36), flags = le16(b + 40);
    uint32_t active = (flags & 0x80) ? flags & 15 : 0;
    v->root = le32(b + 44);
    if (le16(b + 510) != 0xaa55 || le16(b + 17) || le16(b + 22) ||
        v->sector_bytes < 512 || v->sector_bytes > 4096 ||
        (v->sector_bytes & (v->sector_bytes - 1)) ||
        !v->cluster_sectors || v->cluster_sectors > 128 ||
        (v->cluster_sectors & (v->cluster_sectors - 1)) ||
        !reserved || active >= fats || !fat_sectors) return false;
    uint64_t first_data = reserved + (uint64_t)fats * fat_sectors;
    if (first_data >= sectors || (uint64_t)sectors * v->sector_bytes > v->image_bytes)
        return false;
    v->data_clusters = (sectors - first_data) / v->cluster_sectors;
    v->fat_entries = (uint64_t)fat_sectors * v->sector_bytes / 4;
    v->fat_offset = (reserved + (uint64_t)active * fat_sectors) * v->sector_bytes;
    v->data_offset = first_data * v->sector_bytes;
    v->num_fats = fats;
    v->first_fat = (uint64_t)reserved * v->sector_bytes;
    v->fat_bytes = (uint64_t)fat_sectors * v->sector_bytes;
    return v->data_clusters <= 0x0fffffee && v->fat_entries >= (uint64_t)v->data_clusters + 2 &&
           valid_cluster(v, v->root);
}

void print_info(const Volume *v) {
    printf("Root cluster (cluster #): %u\nBytes per sector: %u\nSectors per cluster: %u\n"
           "Total clusters in data region: %u\nEntries in one FAT: %" PRIu64
           "\nImage size (bytes): %" PRIu64 "\n", v->root, v->sector_bytes,
           v->cluster_sectors, v->data_clusters, v->fat_entries, v->image_bytes);
}

static void short_name(const unsigned char *p, char *out) {
    size_t n = 0;
    for (int i = 0; i < 11; i++) {
        if (p[i] == ' ') continue;
        if (i == 8) out[n++] = '.';
        unsigned char c = (i == 0 && p[i] == 5) ? 0xe5 : p[i];
        out[n++] = (p[12] & (i < 8 ? 8 : 16)) ? tolower(c) : c;
    }
    out[n] = 0;
}

static bool same_name(const char *a, const char *b) {
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == *b;
}

int scan_directory(const Volume *v, uint32_t start, const char *target, Entry *match) {
    uint32_t cluster = start;
    size_t bytes = v->sector_bytes * v->cluster_sectors;
    unsigned char *buf = malloc(bytes);
    unsigned char *seen = calloc(((size_t)v->data_clusters + 7) / 8, 1);
    int result = -1;
    if (!buf || !seen) goto done;
    while (valid_cluster(v, cluster)) {
        uint32_t index = cluster - 2;
        unsigned char mask = 1u << (index % 8);
        if (seen[index / 8] & mask) break;
        seen[index / 8] |= mask;
        if (!read_at(v->file, v->data_offset + (uint64_t)index * bytes, buf, bytes)) break;
        for (size_t i = 0; i < bytes; i += 32) {
            unsigned char *p = buf + i;
            if (!p[0]) goto end_of_dir;
            //Skip deleted entries, long-name slots and volume labels.
            if (p[0] == 0xe5 || (p[11] & 0x08)) continue;
            Entry e;
            short_name(p, e.name);
            e.directory = (p[11] & 0x10) != 0;
            e.cluster = ((le16(p + 20) & 0xfff) << 16) | le16(p + 26);
            if (!target) {
                if (strcmp(e.name, ".") && strcmp(e.name, "..")) puts(e.name);
            } else if (same_name(target, e.name)) {
                if (e.directory && !valid_cluster(v, e.cluster)) goto done;
                *match = e;
                result = 1;
                goto done;
            }
        }
        unsigned char link[4];
        if (!read_at(v->file, v->fat_offset + (uint64_t)cluster * 4, link, 4)) break;
        cluster = le32(link) & 0x0fffffff;
        if (cluster >= 0x0ffffff8) goto end_of_dir;
    }
    goto done;
end_of_dir:
    result = target ? 0 : 1;
done:
    free(seen);
    free(buf);
    return result;
}
