#include "update.h"
#include "fsutil.h"
#include "openfile.h"
#include <string.h>

// mv SOURCE DESTINATION - renames a file/directory or moves it to another directory
void do_mv(const Volume *v, uint32_t current,
           const char *source, const char *dest) {
    DirEntry src, dst;

    // prevent manipulation of special directory entries representing current and parent
    if (!strcmp(source, ".") || !strcmp(source, "..") ||
        !strcmp(dest, ".") || !strcmp(dest, "..")) {
        printf("mv: invalid source or destination\n");
        return;
    }

    // locate source file or directory in current working directory
    if (find_entry(v, current, source, &src) != 1) {
        printf("mv: %s: does not exist\n", source);
        return;
    }

    // files must be closed before they can be moved or renamed
    if (find_open_file(current, source) != NULL) {
        printf("mv: %s: file is open\n", source);
        return;
    }

    // FAT32 short filenames occupy 11 bytes
    char new_name[11];

    if (!make_short_name(dest, new_name)) {
        printf("mv: invalid destination name\n");
        return;
    }

    // 1 = entry found, 0 = entry does not exist, -1 = error while searching
    int found = find_entry(v, current, dest, &dst);

    if (found < 0) {
        printf("mv: could not read destination\n");
        return;
    }

    // CASE 1: destination doesn't exist, rename source
    if (found == 0) {
        // first 11 bytes contain short filename, replace only these bytes to keep everything else intact
        memcpy(src.raw, new_name, 11);
        // clear lowercase filename flags because make_short_name produces uppercase name
        src.raw[12] = 0;

        // write modified entry back to its original position in disk image
        if (!save_entry(v, &src))
            printf("mv: could not rename entry\n");

        return;
    }

    // CASE 2: destination exists, it must be a directory.
    if (!entry_is_dir(&dst)) {
        printf("mv: destination is a file\n");
        return;
    }

    // retrieve destination directory's starting cluster
    uint32_t dest_cluster = entry_cluster(&dst);

    // ensure destination cluster is valid
    if (dest_cluster < 2 ||
        dest_cluster - 2 >= v->data_clusters) {
        printf("mv: invalid destination cluster\n");
        return;
    }

    // prevent moving a directory into itself
    if (entry_is_dir(&src) &&
        entry_cluster(&src) == dest_cluster) {
        printf("mv: cannot move directory into itself\n");
        return;
    }

    // make sure destination doesn't already contain an entry with the source's name
    DirEntry existing;

    // return value of 0 means name is available
    if (find_entry(v, dest_cluster, source, &existing) != 0) {
        printf("mv: destination conflict or read error\n");
        return;
    }

    // if source is a directory, its ".." entry must be updated to point to its new parent
    DirEntry parent;
    bool moving_dir = entry_is_dir(&src);

    // locate ".." entry inside the source directory before making any modifications to the images
    if (moving_dir &&
        find_entry(v, entry_cluster(&src), "..", &parent) != 1) {
        printf("mv: could not locate parent entry\n");
        return;
    }

    // find an empty slot in destination directory
    uint64_t new_pos;

    // may allocate another cluster for the destination directory if its currently full
    if (!find_free_slot(v, dest_cluster, &new_pos)) {
        printf("mv: could not find free directory slot\n");
        return;
    }

    // copy source entry into destination
    if (!write_bytes(v, new_pos, src.raw, 32)) {
        printf("mv: could not copy directory entry\n");
        return;
    }

    // update .. if moving a directory.
    if (moving_dir) {
        // FAT32 represents a root parent using cluster 0
        set_entry_cluster(&parent,
            dest_cluster == v->root ? 0 : dest_cluster);

        // persist the updated ".." entry to the disk image
        if (!save_entry(v, &parent)) {
            printf("mv: could not update parent entry\n");
            return;
        }
    }

    // remove original entry WITHOUT freeing its clusters
    if (!delete_entry(v, &src)) {
        printf("mv: could not remove original entry\n");
    }
}

// cluster allocation helper
static uint32_t allocate_data_cluster(const Volume *v) {
    // search FAT for a free cluster
    uint32_t cluster = alloc_cluster(v);

    // return value of 0 indicates that no cluster could be allocated
    if (cluster == 0)
        return 0;

    // clear any leftover data from previous files that may have occupied this cluster
    if (!zero_cluster(v, cluster)) {
        // if initialization fails, release cluster
        set_fat(v, cluster, FAT_FREE);
        return 0;
    }

    return cluster;
}

// returns next cluster in a chain; if current cluster is last one, allocate and attach a new cluster
static uint32_t get_or_extend_cluster(const Volume *v,
                                      uint32_t cluster) {
    uint32_t next = get_fat(v, cluster);

    // if next cluster is already allocated and valid, simply return it
    if (next >= 2 && next - 2 < v->data_clusters)
        return next;

    // Anything other than end-of-chain is an invalid link
    if (next < 0x0FFFFFF8)
        return 0;

    // we have reached the end of the existing chain; allocate and link a new cluster
    next = allocate_data_cluster(v);

    // no free clusters or allocation failed
    if (next == 0)
        return 0;

    // replace current cluster's end-of-chain marker with the number of the newly allocated cluster
    if (!set_fat(v, cluster, next)) {
        // if linking fails, attempt to release newly allocated cluster
        set_fat(v, next, FAT_FREE);
        return 0;
    }

    return next;
}

// write FILENAME "STRING" - writes a string at the file's current offset
void do_write(const Volume *v, uint32_t current,
              const char *name, const char *string) {
    DirEntry entry;

    // VALIDATE TARGET FILE

    if (find_entry(v, current, name, &entry) != 1) {
        printf("write: %s: file does not exist\n", name);
        return;
    }

    if (entry_is_dir(&entry)) {
        printf("write: %s: is a directory\n", name);
        return;
    }

    OpenFile *f = find_open_file(current, name);

    if (f == NULL || !f->can_write) {
        printf("write: file is not open for writing\n");
        return;
    }

    // RETRIEVE FILE INFORMATION

    uint32_t file_size = entry_size(&entry);
    uint32_t csize = cluster_size(v);
    size_t length = strlen(string);

    if (f->offset > file_size ||
        length > UINT32_MAX - f->offset) {
        printf("write: invalid offset or file size\n");
        return;
    }

    uint32_t size = (uint32_t)length;
    uint32_t cluster = entry_cluster(&entry);

    if (size == 0)
        return;

    // HANDLE INITIALLY EMPTY FILES

    // allocate its first cluster
    if (cluster == 0) {
        if (file_size != 0) {
            printf("write: invalid starting cluster\n");
            return;
        }

        cluster = allocate_data_cluster(v);

        if (cluster == 0) {
            printf("write: no free space\n");
            return;
        }

        set_entry_cluster(&entry, cluster);

        if (!save_entry(v, &entry)) {
            set_fat(v, cluster, FAT_FREE);
            printf("write: could not update entry\n");
            return;
        }
    }

    if (cluster < 2 || cluster - 2 >= v->data_clusters) {
        printf("write: invalid cluster\n");
        return;
    }

    // LOCATE STARTING WRITE POSITION

    // Locate cluster corresponding to the file offset
    for (uint32_t i = 0; i < f->offset / csize; i++) {
        cluster = get_or_extend_cluster(v, cluster);

        if (cluster == 0) {
            printf("write: could not reach offset\n");
            return;
        }
    }

    uint32_t start = f->offset % csize;
    uint32_t done = 0;

    // WRITE THE ACTUAL DATA

    // write one cluster-sized portion at a time
    while (done < size) {
        uint32_t amount = csize - start;

        if (amount > size - done)
            amount = size - done;

        uint64_t pos = cluster_offset(v, cluster) + start;

        if (!write_bytes(v, pos, string + done, amount)) {
            printf("write: could not write data\n");
            break;
        }

        done += amount;
        start = 0;

        // Advance only if there is more data to write.
        if (done < size) {
            cluster = get_or_extend_cluster(v, cluster);

            if (cluster == 0) {
                printf("write: could not extend file\n");
                break;
            }
        }
    }

    // UPDATE FILE METADATA

    // update the open file's offset
    f->offset += done;

    // update the file's size if it grew
    if (f->offset > file_size) {
        set_entry_size(&entry, f->offset);

        if (!save_entry(v, &entry))
            printf("write: could not update file size\n");
    }
}