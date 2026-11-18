# FAT32 shell — Parts 1 and 2

Build on Linux or Ubuntu under WSL using GCC and GNU Make. From this folder run:

```sh
make
./filesys /path/to/fat32.img
```

The image argument points to a FAT32 volume with its boot sector at byte zero.
The shell opens it read-only. No system mount or elevated privileges are needed.
Use `make clean` to remove generated files.

| Command | Behavior |
| --- | --- |
| `info` | Print the six requested geometry fields. |
| `ls` | List names in the current directory, including one line each for dot and dot-dot. The shell supplies these names at the root as well. |
| `cd DIRNAME` | Enter an immediate child directory. Give exactly one name. |
| `cd .` / `cd ..` | Stay here / go to the parent. At the root, either stays at the root. |
| `exit` | Close the image and release shell resources. EOF also ends the session. |

## How the code works

- `src/main.c` opens the image, dispatches commands, and tracks the path as a stack of directory names and cluster numbers. A successful descent pushes the on-disk name. Moving to the parent pops it.
- `src/fat32.c` reads little-endian fields from the boot sector. The first data sector follows the reserved sectors and all FAT copies. Subtracting that sector number from the volume sector count and dividing by sectors per cluster gives the data cluster count. One FAT contains (sectors per FAT × bytes per sector) / 4 entries.
- To find cluster N, start at the data region and advance (N − 2) cluster lengths. Both listing and lookup scan 32-byte entries. When a cluster fills, the scanner reads the next cluster number from the FAT. It skips deleted entries, volume labels and long-name slots, using FAT short names for listing and lookup.
- `src/lexer.c` and `include/lexer.h` provide the starter token-list interface. The command loop replaces the starter's token-printing demo.

## Try it with the supplied image

Enter `info` and `ls` at the root. Then enter `cd blue`, `cd blue1`, and `ls`.
The prompt should read `fat32.img/BLUE/BLUE1/>` and the listing should include
`BLUE2`. Use `cd ..` twice to return to the root. From there, `cd hello`
should report that the name belongs to a file, while `cd missing` should
report a missing directory. Use `exit` to finish.
