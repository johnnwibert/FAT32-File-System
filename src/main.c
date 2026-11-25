#include "fat32.h"
#include "lexer.h"
#include <string.h>
#include "create.h"
#include "openfile.h"
#include "update.h"
#include "delete.h"
#include <stdlib.h>
typedef struct {
    uint32_t cluster;
    char *name;
} Directory;

typedef struct {
    Directory *items;
    size_t depth;
} Path;

static void up(Path *path) {
    if (path->depth) free(path->items[--path->depth].name);
}

static void cd(const Volume *v, Path *path, uint32_t current, const char *name) {
    if (!strcmp(name, ".")) return;
    if (!strcmp(name, "..")) { up(path); return; }
    Entry entry;
    int found = scan_directory(v, current, name, &entry);
    if (found < 0) {
        fprintf(stderr, "cd: cannot read directory or invalid cluster\n");
    } else if (!found) {
        fprintf(stderr, "cd: %s: no such directory\n", name);
    } else if (!entry.directory) {
        fprintf(stderr, "cd: %s: not a directory\n", name);
    } else {
        char *copy = malloc(strlen(entry.name) + 1);
        if (!copy) { perror("cd"); return; }
        strcpy(copy, entry.name);
        Directory *items = realloc(path->items, (path->depth + 1) * sizeof *items);
        if (!items) { free(copy); perror("cd"); return; }
        path->items = items;
        items[path->depth++] = (Directory){entry.cluster, copy};
    }
}

//builds the current path like for lsof
static void path_string(const Path *path, char *out, size_t size) {
    strcpy(out, "/");
    for (size_t i = 0; i < path->depth; i++) {
        if (i > 0) strncat(out, "/", size - strlen(out) - 1);
        strncat(out, path->items[i].name, size - strlen(out) - 1);
    }
}
//Parts 3, 4, 5, and 6 commands. Returns false if cmd isn't one of them
static bool run_file_command(const Volume *v, const Path *path, uint32_t current,
                             tokenlist *tokens) {
    const char *cmd = tokens->items[0];
    size_t n = tokens->size;

    if (!strcmp(cmd, "mkdir")) {
        if (n == 2) do_mkdir(v, current, tokens->items[1]);
        else fprintf(stderr, "Usage: mkdir DIRNAME\n");
    } else if (!strcmp(cmd, "creat")) {
        if (n == 2) do_creat(v, current, tokens->items[1]);
        else fprintf(stderr, "Usage: creat FILENAME\n");
    } else if (!strcmp(cmd, "open")) {
        if (n == 3) {
            char dir_path[MAX_PATH_LEN];
            path_string(path, dir_path, sizeof dir_path);
            do_open(v, current, dir_path, tokens->items[1], tokens->items[2]);
        } else fprintf(stderr, "Usage: open FILENAME FLAGS\n");
    } else if (!strcmp(cmd, "close")) {
        if (n == 2) do_close(v, current, tokens->items[1]);
        else fprintf(stderr, "Usage: close FILENAME\n");
    } else if (!strcmp(cmd, "lsof")) {
        if (n == 1) do_lsof();
        else fprintf(stderr, "Usage: lsof\n");
    } else if (!strcmp(cmd, "lseek")) {
        if (n == 3) do_lseek(v, current, tokens->items[1], tokens->items[2]);
        else fprintf(stderr, "Usage: lseek FILENAME OFFSET\n");
    } else if (!strcmp(cmd, "read")) {
        if (n == 3) do_read(v, current, tokens->items[1], tokens->items[2]);
        else fprintf(stderr, "Usage: read FILENAME SIZE\n");
    } else if (!strcmp(cmd, "write")) {
        if (n == 3)
            do_write(v, current, tokens->items[1], tokens->items[2]);
        else
            fprintf(stderr, "Usage: write FILENAME \"STRING\"\n");
    } else if (!strcmp(cmd, "mv")) {
        if (n == 3)
            do_mv(v, current, tokens->items[1], tokens->items[2]);
        else
            fprintf(stderr, "Usage: mv SOURCE DESTINATION\n");
    } else if (!strcmp(cmd, "rm")) {
        if (n == 2)
            do_rm(v, current, tokens->items[1]);
        else
            fprintf(stderr, "Usage: rm FILENAME\n");
    } else if (!strcmp(cmd, "rmdir")) {
        if (n == 2)
            do_rmdir(v, current, tokens->items[1]);
        else
            fprintf(stderr, "Usage: rmdir DIRNAME\n");
    } else {
        return false;
    }
    return true;
}



static void shell(const Volume *v, const char *image) {
    Path path = {0};
    for (;;) {
        printf("%s/", image);
        for (size_t i = 0; i < path.depth; i++) printf("%s/", path.items[i].name);
        printf("> ");
        fflush(stdout);
        char *line = get_input();
        if (!line) break;
        tokenlist *tokens = get_tokens(line);
        const char *cmd = tokens->size ? tokens->items[0] : "";
        bool quit = false;
        uint32_t current = path.depth ? path.items[path.depth - 1].cluster : v->root;
        if (!strcmp(cmd, "cd")) {
            if (tokens->size == 2) cd(v, &path, current, tokens->items[1]);
            else fprintf(stderr, "Usage: cd DIRNAME\n");
        } else if (!strcmp(cmd, "exit") || !strcmp(cmd, "info") || !strcmp(cmd, "ls")) {
            if (tokens->size != 1) fprintf(stderr, "Usage: %s\n", cmd);
            else if (!strcmp(cmd, "exit")) quit = true;
            else if (!strcmp(cmd, "info")) print_info(v);
            else {
                puts(".");
                puts("..");
                if (scan_directory(v, current, NULL, NULL) < 0)
                    fprintf(stderr, "ls: cannot read directory or invalid FAT chain\n");
            }
        } else if (*cmd &&
                !run_file_command(v, &path, current, tokens)) {
            fprintf(stderr, "Unknown command: %s\n", cmd);
        }
        free_tokens(tokens);
        free(line);
        if (quit) break;
    }
    while (path.depth) up(&path);
    free(path.items);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: %s FAT32_IMAGE\n", argv[0]); return 1; }
    Volume volume = {.file = fopen(argv[1], "r+b")};
    if (!volume.file) { perror(argv[1]); return 1; }
    bool ok = read_volume(&volume);
    if (ok) {
        const char *slash = strrchr(argv[1], '/');
        shell(&volume, slash ? slash + 1 : argv[1]);
    } else fprintf(stderr, "%s: invalid or truncated FAT32 volume\n", argv[1]);
    fclose(volume.file);
    return ok ? 0 : 1;
}
