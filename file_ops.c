#include "file_ops.h"
#include <time.h>

// 计算每个块可以容纳多少个inode
#define INODES_PER_BLOCK (BLOCK_SIZE / sizeof(inode_t))

/**
 * 格式化磁盘
 */
int format_disk() {
    // 初始化超级块
    memset(&fs.superblock, 0, sizeof(superblock_t));
    fs.superblock.magic = 0x12345678;
    fs.superblock.blocks = DISK_BLOCKS;
    fs.superblock.inode_blocks = INODE_BLOCKS;
    fs.superblock.data_blocks = DATA_BLOCKS;
    fs.superblock.free_inode_count = MAX_FILES - 1; // 保留根目录inode
    fs.superblock.free_data_count = DATA_BLOCKS - 1; // 保留根目录数据块
    fs.superblock.state = 1; // 已挂载
    
    // 写入超级块
    disk_write_block(SUPERBLOCK_BLOCK, &fs.superblock);
    
    // 初始化inode位图
    memset(fs.inode_bitmap, 0, sizeof(fs.inode_bitmap));
    fs.inode_bitmap[0] = 1; // 标记根目录inode已被使用
    
    // 写入inode位图
    disk_write_block(INODE_BITMAP_BLOCK, fs.inode_bitmap);
    
    // 初始化数据块位图
    memset(fs.data_bitmap, 0, sizeof(fs.data_bitmap));
    fs.data_bitmap[0] = 1; // 标记根目录数据块已被使用
    
    // 写入数据块位图
    disk_write_block(DATA_BITMAP_BLOCK, fs.data_bitmap);
    
    // 初始化根目录inode
    inode_t root_inode = {0};
    root_inode.type = 2; // 目录
    root_inode.links = 1;
    root_inode.blocks[0] = DATA_START_BLOCK; // 根目录数据块
    
    // 写入根目录inode (根目录inode编号为0，位于第0个inode块的第0个inode)
    char inode_block[BLOCK_SIZE] = {0};
    memcpy(inode_block, &root_inode, sizeof(inode_t));
    disk_write_block(INODE_START_BLOCK, inode_block);
    
    // 初始化根目录数据块（空目录）
    char root_data[BLOCK_SIZE] = {0};
    disk_write_block(DATA_START_BLOCK, root_data);
    
    printf("磁盘格式化完成\n");
    return 0;
}

/**
 * 显示磁盘信息
 */
int show_disk_info() {
    printf("\n磁盘信息:\n");
    printf("  总块数: %u\n", fs.superblock.blocks);
    printf("  Inode区块数: %u\n", fs.superblock.inode_blocks);
    printf("  数据区块数: %u\n", fs.superblock.data_blocks);
    printf("  空闲Inode数: %u\n", fs.superblock.free_inode_count);
    printf("  空闲数据块数: %u\n", fs.superblock.free_data_count);
    printf("  文件系统状态: %s\n", fs.superblock.state ? "已挂载" : "未挂载");
    printf("\n");
    return 0;
}

/**
 * 分配一个inode
 */
int alloc_inode() {
    if (fs.superblock.free_inode_count <= 0) {
        return -1; // 没有空闲inode
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (!(fs.inode_bitmap[i / 8] & (1 << (i % 8)))) {
            fs.inode_bitmap[i / 8] |= (1 << (i % 8));
            fs.superblock.free_inode_count--;
            
            // 更新磁盘上的位图和超级块
            disk_write_block(INODE_BITMAP_BLOCK, fs.inode_bitmap);
            disk_write_block(SUPERBLOCK_BLOCK, &fs.superblock);
            
            return i;
        }
    }
    
    return -1; // 没有找到空闲inode
}

/**
 * 释放一个inode
 */
void free_inode(int inode_num) {
    if (inode_num < 0 || inode_num >= MAX_FILES) {
        return;
    }
    
    fs.inode_bitmap[inode_num / 8] &= ~(1 << (inode_num % 8));
    fs.superblock.free_inode_count++;
    
    // 更新磁盘上的位图和超级块
    disk_write_block(INODE_BITMAP_BLOCK, fs.inode_bitmap);
    disk_write_block(SUPERBLOCK_BLOCK, &fs.superblock);
}

/**
 * 分配一个数据块
 */
int alloc_block() {
    if (fs.superblock.free_data_count <= 0) {
        return -1; // 没有空闲数据块
    }
    
    for (int i = 0; i < DATA_BLOCKS; i++) {
        if (!(fs.data_bitmap[i / 8] & (1 << (i % 8)))) {
            fs.data_bitmap[i / 8] |= (1 << (i % 8));
            fs.superblock.free_data_count--;
            
            // 更新磁盘上的位图和超级块
            disk_write_block(DATA_BITMAP_BLOCK, fs.data_bitmap);
            disk_write_block(SUPERBLOCK_BLOCK, &fs.superblock);
            
            return DATA_START_BLOCK + i;
        }
    }
    
    return -1; // 没有找到空闲数据块
}

/**
 * 释放一个数据块
 */
void free_block(int block_num) {
    if (block_num < DATA_START_BLOCK || block_num >= DISK_BLOCKS) {
        return;
    }
    
    int data_block_index = block_num - DATA_START_BLOCK;
    fs.data_bitmap[data_block_index / 8] &= ~(1 << (data_block_index % 8));
    fs.superblock.free_data_count++;
    
    // 更新磁盘上的位图和超级块
    disk_write_block(DATA_BITMAP_BLOCK, fs.data_bitmap);
    disk_write_block(SUPERBLOCK_BLOCK, &fs.superblock);
}

/**
 * 创建文件
 */
int create_file(const char* filename) {
    // 查找根目录中是否已存在同名文件
    inode_t root_inode;
    disk_read_block(INODE_START_BLOCK, &root_inode);
    
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode.blocks[0], root_data);
    
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    int free_slot = -1;
    
    // 查找同名文件或空槽位
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].inode != 0) {
            // 检查是否已存在同名文件
            if (strcmp(entries[i].name, filename) == 0) {
                printf("错误: 文件 '%s' 已存在\n", filename);
                return -1;
            }
        } else if (free_slot == -1) {
            // 记录第一个空槽位
            free_slot = i;
        }
    }
    
    // 如果没有找到空槽位，再次完整遍历确认
    if (free_slot == -1) {
        for (int i = 0; i < entry_count; i++) {
            if (entries[i].inode == 0) {
                free_slot = i;
                break;
            }
        }
    }
    
    // 如果仍然没有找到空槽位，说明目录确实已满
    if (free_slot == -1) {
        printf("错误: 目录已满\n");
        return -1;
    }
    
    // 分配inode
    int inode_num = alloc_inode();
    if (inode_num < 0) {
        printf("错误: 没有可用的inode\n");
        return -1;
    }
    
    // 初始化inode
    inode_t new_inode = {0};
    new_inode.type = 1; // 普通文件
    new_inode.links = 1;
    new_inode.size = 0;
    
    // 计算inode所在的块和在块内的偏移
    int inode_block_index = inode_num / INODES_PER_BLOCK;
    int inode_offset = inode_num % INODES_PER_BLOCK;
    
    // 读取inode所在的块
    char inode_block[BLOCK_SIZE];
    disk_read_block(INODE_START_BLOCK + inode_block_index, inode_block);
    
    // 更新inode
    inode_t* inodes = (inode_t*)inode_block;
    inodes[inode_offset] = new_inode;
    
    // 写入inode块
    disk_write_block(INODE_START_BLOCK + inode_block_index, inode_block);
    
    // 更新目录项
    entries[free_slot].inode = inode_num;
    strncpy(entries[free_slot].name, filename, MAX_FILENAME - 1);
    entries[free_slot].name[MAX_FILENAME - 1] = '\0';
    
    // 写回根目录数据块
    disk_write_block(root_inode.blocks[0], root_data);
    
    printf("文件 '%s' 创建成功\n", filename);
    return 0;
}

/**
 * 删除文件
 */
int delete_file(const char* filename) {
    // 查找根目录中的文件
    inode_t root_inode;
    disk_read_block(INODE_START_BLOCK, &root_inode);
    
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode.blocks[0], root_data);
    
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    int file_index = -1;
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].inode != 0 && strcmp(entries[i].name, filename) == 0) {
            file_index = i;
            break;
        }
    }
    
    if (file_index == -1) {
        printf("错误: 文件 '%s' 不存在\n", filename);
        return -1;
    }
    
    // 获取文件inode
    int inode_num = entries[file_index].inode;
    int inode_block_index = inode_num / INODES_PER_BLOCK;
    int inode_offset = inode_num % INODES_PER_BLOCK;
    
    char inode_block[BLOCK_SIZE];
    disk_read_block(INODE_START_BLOCK + inode_block_index, inode_block);
    inode_t* inodes = (inode_t*)inode_block;
    inode_t file_inode = inodes[inode_offset];
    
    // 释放数据块
    for (int i = 0; i < 8 && file_inode.blocks[i] != 0; i++) {
        free_block(file_inode.blocks[i]);
    }
    
    // 释放inode
    free_inode(inode_num);
    
    // 清除目录项
    entries[file_index].inode = 0;
    memset(entries[file_index].name, 0, MAX_FILENAME);
    
    // 写回根目录数据块
    disk_write_block(root_inode.blocks[0], root_data);
    
    printf("文件 '%s' 删除成功\n", filename);
    return 0;
}

/**
 * 列出目录内容
 */
int list_directory() {
    inode_t root_inode;
    disk_read_block(INODE_START_BLOCK, &root_inode);
    
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode.blocks[0], root_data);
    
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    
    printf("目录内容:\n");
    int file_count = 0;
    for (int i = 0; i < entry_count; i++) {
        // 只显示有效的目录项（inode不为0的项）
        if (entries[i].inode != 0) {
            // 获取文件inode
            int inode_num = entries[i].inode;
            int inode_block_index = inode_num / INODES_PER_BLOCK;
            int inode_offset = inode_num % INODES_PER_BLOCK;
            
            char inode_block[BLOCK_SIZE];
            disk_read_block(INODE_START_BLOCK + inode_block_index, inode_block);
            inode_t* inodes = (inode_t*)inode_block;
            inode_t file_inode = inodes[inode_offset];
            
            const char* type_str = (file_inode.type == 1) ? "文件" : (file_inode.type == 2) ? "目录" : "未知";
            printf("  %-20s [%s, 大小: %u 字节]\n", entries[i].name, type_str, file_inode.size);
            file_count++;
        }
    }
    
    if (file_count == 0) {
        printf("  (空目录)\n");
    }
    printf("\n");
    
    return 0;
}

/**
 * 读取文件内容
 */
int read_file(const char* filename, char* buffer, size_t size) {
    // 查找根目录中的文件
    inode_t root_inode;
    disk_read_block(INODE_START_BLOCK, &root_inode);
    
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode.blocks[0], root_data);
    
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    int file_index = -1;
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].inode != 0 && strcmp(entries[i].name, filename) == 0) {
            file_index = i;
            break;
        }
    }
    
    if (file_index == -1) {
        printf("错误: 文件 '%s' 不存在\n", filename);
        return -1;
    }
    
    // 获取文件inode
    int inode_num = entries[file_index].inode;
    int inode_block_index = inode_num / INODES_PER_BLOCK;
    int inode_offset = inode_num % INODES_PER_BLOCK;
    
    char inode_block[BLOCK_SIZE];
    disk_read_block(INODE_START_BLOCK + inode_block_index, inode_block);
    inode_t* inodes = (inode_t*)inode_block;
    inode_t file_inode = inodes[inode_offset];
    
    if (file_inode.type != 1) {
        printf("错误: '%s' 不是一个普通文件\n", filename);
        return -1;
    }
    
    // 读取文件内容
    size_t bytes_to_read = (size < file_inode.size) ? size : file_inode.size;
    size_t bytes_read = 0;
    
    for (int i = 0; i < 8 && bytes_read < bytes_to_read && file_inode.blocks[i] != 0; i++) {
        char block_data[BLOCK_SIZE];
        disk_read_block(file_inode.blocks[i], block_data);
        
        size_t block_bytes = (bytes_to_read - bytes_read < BLOCK_SIZE) ? (bytes_to_read - bytes_read) : BLOCK_SIZE;
        memcpy(buffer + bytes_read, block_data, block_bytes);
        bytes_read += block_bytes;
    }
    
    return bytes_read;
}

/**
 * 写入文件内容
 */
int write_file(const char* filename, const char* buffer, size_t size) {
    // 查找根目录中的文件
    inode_t root_inode;
    disk_read_block(INODE_START_BLOCK, &root_inode);
    
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode.blocks[0], root_data);
    
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    int file_index = -1;
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].inode != 0 && strcmp(entries[i].name, filename) == 0) {
            file_index = i;
            break;
        }
    }
    
    if (file_index == -1) {
        printf("错误: 文件 '%s' 不存在\n", filename);
        return -1;
    }
    
    // 获取文件inode
    int inode_num = entries[file_index].inode;
    int inode_block_index = inode_num / INODES_PER_BLOCK;
    int inode_offset = inode_num % INODES_PER_BLOCK;
    
    char inode_block[BLOCK_SIZE];
    disk_read_block(INODE_START_BLOCK + inode_block_index, inode_block);
    inode_t* inodes = (inode_t*)inode_block;
    inode_t file_inode = inodes[inode_offset];
    
    if (file_inode.type != 1) {
        printf("错误: '%s' 不是一个普通文件\n", filename);
        return -1;
    }
    
    // 释放原有数据块
    for (int i = 0; i < 8 && file_inode.blocks[i] != 0; i++) {
        free_block(file_inode.blocks[i]);
        file_inode.blocks[i] = 0;
    }
    
    // 写入文件内容
    size_t bytes_written = 0;
    int block_index = 0;
    
    while (bytes_written < size && block_index < 8) {
        // 分配新的数据块
        int new_block = alloc_block();
        if (new_block < 0) {
            printf("错误: 磁盘空间不足\n");
            break;
        }
        file_inode.blocks[block_index] = new_block;
        
        // 准备要写入的数据块
        char block_data[BLOCK_SIZE] = {0};
        size_t block_bytes = (size - bytes_written < BLOCK_SIZE) ? (size - bytes_written) : BLOCK_SIZE;
        memcpy(block_data, buffer + bytes_written, block_bytes);
        
        // 写入数据块
        disk_write_block(file_inode.blocks[block_index], block_data);
        
        bytes_written += block_bytes;
        block_index++;
    }
    
    // 更新文件大小
    file_inode.size = bytes_written;
    
    // 写回inode
    inodes[inode_offset] = file_inode;
    disk_write_block(INODE_START_BLOCK + inode_block_index, inode_block);
    
    printf("向文件 '%s' 写入了 %zu 字节\n", filename, bytes_written);
    return bytes_written;
}