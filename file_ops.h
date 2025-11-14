#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "disk.h"

#define MAX_FILENAME 32
#define MAX_FILES 128

// 目录项结构
typedef struct {
  uint32_t inode;
  char name[MAX_FILENAME];
} dir_entry_t;

// 格式化磁盘，初始化文件系统
int format_disk();

// 显示磁盘信息，包括总大小、已用空间和空闲空间
int show_disk_info();

// 分配一个inode节点
int alloc_inode();

// 释放指定的inode节点
void free_inode(int inode_num);

// 分配一个数据块
int alloc_block();

// 释放指定的数据块
void free_block(int block_num);

// 创建新文件
int create_file(const char *filename);

// 删除指定文件
int delete_file(const char *filename);

// 从文件读取数据
int read_file(const char *filename, char *buffer, size_t size);

// 向文件写入数据
int write_file(const char *filename, const char *buffer, size_t size);

// 列出目录中的所有文件
int list_directory();

#endif