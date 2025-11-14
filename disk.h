#ifndef DISK_H
#define DISK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BLOCK_SIZE 512                 // 磁盘块大小(字节)
#define DISK_BLOCKS 4096               // 磁盘总块数
#define DISK_SIZE (BLOCK_SIZE * DISK_BLOCKS)  // 磁盘总大小

#define SUPERBLOCK_BLOCK 0             // 超级块位置
#define INODE_BITMAP_BLOCK 1           // inode位图起始块
#define DATA_BITMAP_BLOCK 2            // 数据块位图起始块
#define INODE_START_BLOCK 3            // inode表起始块
#define INODE_BLOCKS 128               // inode表占用块数
#define DATA_START_BLOCK (INODE_START_BLOCK + INODE_BLOCKS)  // 数据区起始块
#define DATA_BLOCKS (DISK_BLOCKS - DATA_START_BLOCK)         // 数据块数量

// inode结构
typedef struct {
    uint32_t size;                     // 文件大小
    uint16_t type;                     // 文件类型 (1: 普通文件, 2: 目录)
    uint16_t links;                    // 链接计数
    uint32_t blocks[8];                // 直接数据块指针 (最多8个块)
} inode_t;

// 超级块结构
typedef struct {
    uint32_t magic;                    // 魔数，标识文件系统
    uint32_t blocks;                   // 总块数
    uint32_t inode_blocks;             // inode区占用块数
    uint32_t data_blocks;              // 数据区可用块数
    uint32_t free_inode_count;         // 空闲inode数
    uint32_t free_data_count;          // 空闲数据块数
    char padding[BLOCK_SIZE - 6*sizeof(uint32_t) - sizeof(uint16_t)];
    uint16_t state;                    // 文件系统状态
} superblock_t;

// 文件系统结构
typedef struct {
    FILE* file;                        // 磁盘映像文件句柄
    superblock_t superblock;          // 超级块缓存
    char inode_bitmap[INODE_BLOCKS * BLOCK_SIZE];  // inode位图缓存
    char data_bitmap[DATA_BLOCKS / 8]; // 数据块位图缓存 (每个bit代表一个数据块)
} filesystem_t;

extern filesystem_t fs;

int disk_init(const char* filename);
void disk_close();
int disk_read_block(uint32_t block_num, void* buffer);
int disk_write_block(uint32_t block_num, const void* buffer);

#endif