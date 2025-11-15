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

// 查找文件并返回其inode编号
int _find_file_inode(const char *filename, inode_t *root_inode);

// 根据inode编号读取inode结构
int _read_inode(int inode_num, inode_t *inode_out);

// 根据inode编号写入inode结构
int _write_inode(int inode_num, inode_t *inode_in);

// 创建文件 (无锁)
int _create_file(const char *filename, inode_t *root_inode);

// 读取文件逻辑 (无锁)
int _read_file(int inode_num, inode_t *file_inode, char *buffer, size_t size);

// 写入文件逻辑 (无锁)
int _write_file(int inode_num, inode_t *file_inode, const char *buffer,
                size_t size);

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

// 复制文件
int copy_file(const char *src_filename, const char *dest_filename);

// 初始化文件系统锁
void init_fs_lock();

#endif