#include "openfile.h"
#include "fsutil.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

//table of open files
static OpenFile open_files[MAX_OPEN_FILES];

//copies name into out in uppercase
static void to_upper(const char *name, char *out) {
    int i;
    for (i = 0; name[i] && i < 8; i++)
        out[i] = toupper((unsigned char)name[i]);
    out[i] = '\0';
}

//returns the open file with this name in this directory
OpenFile *find_open_file(uint32_t dir_cluster, const char *name) {
    char upper[9];
    to_upper(name, upper);
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].used && open_files[i].dir_cluster == dir_cluster &&
            strcmp(open_files[i].name, upper) == 0)
            return &open_files[i];
    }
    return NULL;
}

//true if a file in this directory is open
bool dir_has_open_files(uint32_t dir_cluster) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].used && open_files[i].dir_cluster == dir_cluster)
            return true;
    }
    return false;
}

//turns a string into a number returns false if it isn't a whole number
static bool get_number(const char *str, uint32_t *out) {
    char *end;
    long value = strtol(str, &end, 10);
    if (*str == '\0' || *end != '\0' || value < 0)
        return false;
    *out = (uint32_t)value;
    return true;
}

//finds a file in the current directory, prints an error if it's missing or is a directory
static bool get_file(const Volume *v, uint32_t current, const char *cmd,
                     const char *name, DirEntry *entry) {
    if (find_entry(v, current, name, entry) != 1) {
        printf("%s: %s: file does not exist\n", cmd, name);
        return false;
    }
    if (entry_is_dir(entry)) {
        printf("%s: %s: is a directory\n", cmd, name);
        return false;
    }
    return true;
}

//open FILENAME FLAGS
void do_open(const Volume *v, uint32_t current, const char *dir_path,
             const char *name, const char *flags) {
    if (strcmp(flags, "-r") != 0 && strcmp(flags, "-w") != 0 &&
        strcmp(flags, "-rw") != 0 && strcmp(flags, "-wr") != 0) {
        printf("open: %s: invalid mode\n", flags);
        return;
    }

    DirEntry entry;
    if (!get_file(v, current, "open", name, &entry))
        return;

    if (find_open_file(current, name) != NULL) {
        printf("open: %s: already open\n", name);
        return;
    }

    //find an unused spot in the table
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].used)
            continue;

        OpenFile *f = &open_files[i];
        f->used = true;
        f->offset = 0;
        f->dir_cluster = current;
        strcpy(f->mode, flags);
        f->can_read = strchr(flags, 'r') != NULL;
        f->can_write = strchr(flags, 'w') != NULL;
        to_upper(name, f->name);
        snprintf(f->path, MAX_PATH_LEN, "%s", dir_path);
        return;
    }
    printf("open: too many open files\n");
}

void do_close(const Volume *v, uint32_t current, const char *name) {
    DirEntry entry;
    if (find_entry(v, current, name, &entry) != 1) {
        printf("close: %s: file does not exist\n", name);
        return;
    }
    OpenFile *f = find_open_file(current, name);
    if (f == NULL) {
        printf("close: %s: file is not open\n", name);
        return;
    }
    f->used = false;
}

void do_lsof(void) {
    bool any = false;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        OpenFile *f = &open_files[i];
        if (!f->used)
            continue;
        if (!any)
            printf("INDEX  NAME      MODE  OFFSET  PATH\n");
        any = true;
        printf("%-6d %-9s %-5s %-7u %s\n", i, f->name, f->mode, f->offset, f->path);
    }
    if (!any)
        printf("No files are open.\n");
}

//lseek FILENAME OFFSET
void do_lseek(const Volume *v, uint32_t current, const char *name, const char *offset_str) {
    DirEntry entry;
    if (!get_file(v, current, "lseek", name, &entry))
        return;

    OpenFile *f= find_open_file(current, name);
    if (f == NULL) {
        printf("lseek: %s: file is not open\n", name);
        return;
    }

    uint32_t offset;
    if (!get_number(offset_str, &offset)) {
        printf("lseek: %s: invalid offset\n", offset_str);
        return;
    }
    if (offset >entry_size(&entry)) {
        printf("lseek: offset is larger than the file size\n");
        return;
    }
    f->offset =offset;
}

//read FILENAME SIZE
void do_read(const Volume *v, uint32_t current, const char *name, const char *size_str) {
    DirEntry entry;
    if (!get_file(v, current, "read",name, &entry))
        return;

    OpenFile *f=find_open_file(current, name);
    if (f == NULL ||!f->can_read) {
        printf("read: %s: file is not open for reading\n", name);
        return;
    }

    uint32_t size;
    if (!get_number(size_str, &size)) {
        printf("read: %s: invalid size\n", size_str);
        return;
    }

    //stop at end of the file
    uint32_t file_size = entry_size(&entry);
    if (size > file_size - f->offset)
        size = file_size - f->offset;

    uint32_t csize = cluster_size(v);
    unsigned char *buf = malloc(csize);
    if (!buf)
        return;

    //move to the cluster the offset is in
    uint32_t cluster = entry_cluster(&entry);
    for (uint32_t i = 0; i < f->offset / csize; i++)
        cluster = get_fat(v, cluster);

    uint32_t start = f->offset % csize;    //where to start in this cluster
    uint32_t done = 0;
    while (done < size && !is_end_of_chain(cluster)) {
        read_bytes(v, cluster_offset(v, cluster), buf, csize);

        uint32_t amount = csize - start;
        if (amount > size - done)
            amount = size - done;
        fwrite(buf + start, 1, amount, stdout);

        done += amount;
        start = 0;
        cluster = get_fat(v, cluster);
    }
    printf("\n");

    f->offset += done;
    free(buf);
}
