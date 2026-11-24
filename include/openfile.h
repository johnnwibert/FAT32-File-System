#ifndef OPENFILE_H
#define OPENFILE_H

#include "fat32.h"

#define MAX_OPEN_FILES 10
#define MAX_PATH_LEN 512

//one slot in the open file table
typedef struct {
    bool used;
    char name[9];
    char mode[4];             //the flag that was used: -r, -w, -rw or -wr 
    bool can_read;
    bool can_write;
    uint32_t offset;          //here the next read/write starts 
    uint32_t dir_cluster;     //first cluster of the directory the file is in
    char path[MAX_PATH_LEN];  //full path of the file for lsof 
} OpenFile;

//Part 4: read commands
void do_open(const Volume *v, uint32_t current, const char *dir_path,
             const char *name, const char *flags);
void do_close(const Volume *v, uint32_t current, const char *name);
void do_lsof(void);
void do_lseek(const Volume *v, uint32_t current, const char *name, const char *offset_str);
void do_read(const Volume *v, uint32_t current, const char *name, const char *size_str);

//helpers the other commands (write, mv, rm, rmdir) can use
OpenFile *find_open_file(uint32_t dir_cluster, const char *name);
bool dir_has_open_files(uint32_t dir_cluster);

#endif
