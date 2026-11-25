#include "create.h"
#include "fsutil.h"
#include <string.h>

//returns true if name is valid and isnt already used in the directory
static bool name_is_available(const Volume *v, uint32_t current,
                              const char *cmd, const char *name) {
    char short_name[11];
    DirEntry existing;

    if (!make_short_name(name, short_name)) {
        printf("%s: %s: invalid name (max 8 characters)\n", cmd, name);
        return false;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        find_entry(v, current, name, &existing) == 1) {
        printf("%s: %s: already exists\n", cmd, name);
        return false;
    }
    return true;
}

//mkdir DIRNAME
void do_mkdir(const Volume *v, uint32_t current, const char *name) {
    if (!name_is_available(v, current, "mkdir", name))
        return;

    uint32_t new_cluster = alloc_cluster(v);
    if (new_cluster == 0) {
        printf("mkdir: no free space left\n");
        return;
    }
    zero_cluster(v, new_cluster);

    uint32_t parent = (current == v->root) ? 0 : current;
    add_entry(v, new_cluster, ".", ATTR_DIRECTORY, new_cluster);
    add_entry(v, new_cluster, "..", ATTR_DIRECTORY, parent);

    if (!add_entry(v, current, name, ATTR_DIRECTORY, new_cluster)) {
        printf("mkdir: could not create %s\n", name);
        set_fat(v, new_cluster, FAT_FREE);
    }
}

void do_creat(const Volume *v, uint32_t current, const char *name) {
    if (!name_is_available(v, current, "creat", name))
        return;

    if (!add_entry(v, current, name, ATTR_ARCHIVE, 0))
        printf("creat: could not create %s\n", name);
}
