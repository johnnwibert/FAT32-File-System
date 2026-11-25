#include "delete.h"
#include "fsutil.h"
#include "openfile.h"
#include <string.h>

// frees every cluster belonging to a file or directory
static bool free_cluster_chain(const Volume *v, uint32_t cluster) {
    // an empty file may not have any allocated clusters to free
    if (cluster == 0)
        return true;

    // limit iterations to the total number of data clusters to prevent an infinite loop if FAT chain is corrupted
    for (uint32_t i = 0; i < v->data_clusters; i++) {
        // make sure current cluser is within valid range of clusters in data region
        if (cluster < 2 || cluster - 2 >= v->data_clusters)
            return false;

        // save the next cluster before clearing the current FAT entry
        uint32_t next = get_fat(v, cluster);

        // mark cluster as free
        if (!set_fat(v, cluster, FAT_FREE))
            return false;

        // FAT32 values >= 0x0FFFFFF8 indicate the end of a cluster chain; all clusters have been freed
        if (next >= 0x0FFFFFF8)
            return true;

        // continue to next cluster
        cluster = next;
    }

    return false;
}

// rm FILENAME - removes regular file and reclaims allocated disk space
void do_rm(const Volume *v, uint32_t current, const char *name) {
    DirEntry entry;

    // search current directory for specified file
    if (find_entry(v, current, name, &entry) != 1) {
        printf("rm: %s: file does not exist\n", name);
        return;
    }

    // rm is only intended for regular files, directories must be removed using rmdir
    if (entry_is_dir(&entry)) {
        printf("rm: %s: is a directory\n", name);
        return;
    }

    // check the open-file table; file must be closed before it can be deleted
    if (find_open_file(current, name) != NULL) {
        printf("rm: %s: file is open\n", name);
        return;
    }

    // mark entry as deleted by replacing first byte with 0xE5
    if (!delete_entry(v, &entry)) {
        printf("rm: could not delete entry\n");
        return;
    }

    // traverse cluster chain and mark every cluster as free
    if (!free_cluster_chain(v, entry_cluster(&entry))) {
        printf("rm: could not reclaim all clusters\n");
    }
}

// returns true if the directory contains only . and ..
static bool is_directory_empty(const Volume *v, uint32_t cluster) {
    unsigned char raw[32];
    uint32_t csize = cluster_size(v);

    for (uint32_t n = 0; n < v->data_clusters; n++) {
        if (cluster < 2 || cluster - 2 >= v->data_clusters)
            return false;

        // each FAT32 entry occupies 32 bytes, examine each entry in the current cluster
        for (uint32_t i = 0; i < csize; i += 32) {
            // calculate entry's absolute byte position within disk image
            uint64_t pos = cluster_offset(v, cluster) + i;

            // read raw directory entry from image
            if (!read_bytes(v, pos, raw, 32))
                return false;

            // 0x00 means there are no more active entries
            if (raw[0] == 0x00)
                return true;

            // Previously deleted entry
            if (raw[0] == 0xE5)
                continue;

            // Skip long filename entries
            if (raw[11] == ATTR_LONG_NAME)
                continue;

            // Ignore . and ..
            if (memcmp(raw, ".          ", 11) == 0 ||
                memcmp(raw, "..         ", 11) == 0)
                continue;

            // Found an active file or directory
            return false;
        }

        cluster = get_fat(v, cluster);

        if (cluster >= 0x0FFFFFF8)
            return true;
    }

    return false;
}

// rmdir DIRNAME - removes an empty directory and reclaims its clusters
void do_rmdir(const Volume *v, uint32_t current, const char *name) {
    DirEntry entry;

    // never allow current or parent directory to be removed through their special names
    if (strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        printf("rmdir: cannot remove %s\n", name);
        return;
    }

    // locate target directory within current working directory
    if (find_entry(v, current, name, &entry) != 1) {
        printf("rmdir: %s: directory does not exist\n", name);
        return;
    }

    // verify that target is actually a directory
    if (!entry_is_dir(&entry)) {
        printf("rmdir: %s: is not a directory\n", name);
        return;
    }

    // retrieve cluster where directory's contents begin
    uint32_t cluster = entry_cluster(&entry);

    // make sure starting clsuter is valid, protect root and current directories
    if (cluster < 2 || cluster - 2 >= v->data_clusters ||
        cluster == v->root || cluster == current) {
        printf("rmdir: invalid directory\n");
        return;
    }

    // check open-file table
    if (dir_has_open_files(cluster)) {
        printf("rmdir: directory contains open files\n");
        return;
    }

    // directory must contain no active entires other than . and ..
    if (!is_directory_empty(v, cluster)) {
        printf("rmdir: directory is not empty or unreadable\n");
        return;
    }

    if (!delete_entry(v, &entry)) {
        printf("rmdir: could not delete entry\n");
        return;
    }

    if (!free_cluster_chain(v, cluster)) {
        printf("rmdir: could not reclaim all clusters\n");
    }
}