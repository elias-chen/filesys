#include "file_ops.h"
#include <time.h>
#include <pthread.h> // 导入 POSIX 线程库

// =============================================================================
// 全局文件系统锁
// 读写操作都必须先获取这个锁。
// =============================================================================
pthread_mutex_t fs_lock;

// 计算每个块可以容纳多少个inode
#define INODES_PER_BLOCK (BLOCK_SIZE / sizeof(inode_t))

// =============================================================================
// 内部辅助函数 (无锁)
// 加锁与 文件系统内部操作之间进行分离

/**
 * [内部] 查找文件并返回其inode编号。
 * 成功返回inode编号，失败返回-1。
 */
int _find_file_inode(const char* filename, inode_t* root_inode) {
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode->blocks[0], root_data);
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].inode != 0 && strcmp(entries[i].name, filename) == 0) {
            return (int)entries[i].inode;  
        }
    }
    
    return -1; 
}

/**
 * [内部] 根据inode编号读取inode结构。
 * 成功返回0并填充inode_out，失败返回-1。
 */
int _read_inode(int inode_num, inode_t* inode_out) {
    if (inode_num < 0 || inode_num >= MAX_FILES) {
        return -1;
    }
    
    int inode_block_index = inode_num / INODES_PER_BLOCK;
    int inode_offset = inode_num % INODES_PER_BLOCK;
    
    char inode_block[BLOCK_SIZE];
    disk_read_block(INODE_START_BLOCK + inode_block_index, inode_block);
    
    inode_t* inodes = (inode_t*)inode_block;
    *inode_out = inodes[inode_offset];
    
    return 0;
}

/**
 * [内部] 根据inode编号写入inode结构。
 * 成功返回0，失败返回-1。
 */
int _write_inode(int inode_num, inode_t* inode_in) {
    if (inode_num < 0 || inode_num >= MAX_FILES) {
        return -1;
    }

    int inode_block_index = inode_num / INODES_PER_BLOCK;
    int inode_offset = inode_num % INODES_PER_BLOCK;

    char inode_block[BLOCK_SIZE];
    // 取包含目标inode的整个块
    disk_read_block(INODE_START_BLOCK + inode_block_index, inode_block);

    // 更新该块中的特定inode
    inode_t* inodes = (inode_t*)inode_block;
    inodes[inode_offset] = *inode_in;

    // 修改后的整个块写回磁盘
    disk_write_block(INODE_START_BLOCK + inode_block_index, inode_block);

    return 0;
}


/**
 * [内部] 创建文件逻辑 (无锁)
 */
int _create_file(const char* filename, inode_t* root_inode) {
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode->blocks[0], root_data);
    
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    int free_slot = -1;
    
    // 查找同名文件或空位
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].inode != 0) {
            if (strcmp(entries[i].name, filename) == 0) {
                printf("错误: 文件 '%s' 已存在\n", filename);
                return -1;
            }
        } else if (free_slot == -1) {
            free_slot = i;
        }
    }
    
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
    
    // inode init
    inode_t new_inode = {0};
    new_inode.type = 1; // 1 是文件
    new_inode.links = 1;
    new_inode.size = 0;
    
    // 写入inode
    _write_inode(inode_num, &new_inode);
    
    // 更新目录项
    entries[free_slot].inode = inode_num;
    strncpy(entries[free_slot].name, filename, MAX_FILENAME - 1);
    entries[free_slot].name[MAX_FILENAME - 1] = '\0';
    
    // 写回根目录
    disk_write_block(root_inode->blocks[0], root_data);
    
    printf("文件 '%s' 创建成功\n", filename);
    return 0; 
}

/**
 * [内部] 读取文件逻辑 (无锁)
 */
int _read_file(int inode_num, inode_t* file_inode, char* buffer, size_t size) {
    (void)inode_num;
    
    if (file_inode->type != 1) {
        return -1; // 不是文件
    }

    size_t bytes_to_read = (size < file_inode->size) ? size : file_inode->size;
    size_t bytes_read = 0;
    
    for (int i = 0; i < 8 && bytes_read < bytes_to_read && file_inode->blocks[i] != 0; i++) {
        char block_data[BLOCK_SIZE];
        disk_read_block(file_inode->blocks[i], block_data);
        
        size_t block_bytes = (bytes_to_read - bytes_read < BLOCK_SIZE) ? (bytes_to_read - bytes_read) : BLOCK_SIZE;
        memcpy(buffer + bytes_read, block_data, block_bytes);
        bytes_read += block_bytes;
    }
    
    return bytes_read;
}

/**
 * [内部] 写入文件逻辑 (无锁)
 */
int _write_file(int inode_num, inode_t* file_inode, const char* buffer, size_t size) {
    // 检查文件类型
    if (file_inode->type != 1) {
        return -1; 
    }

    // 释放原来数据块
    for (int i = 0; i < 8 && file_inode->blocks[i] != 0; i++) {
        free_block(file_inode->blocks[i]);
        file_inode->blocks[i] = 0;
    }
    
    // 写入文件
    size_t bytes_written = 0;
    int block_index = 0;
    
    while (bytes_written < size && block_index < 8) {
        // 分配新的数据块
        int new_block = alloc_block();
        if (new_block < 0) {
            printf("错误: 磁盘空间不足\n");
            break;
        }
        file_inode->blocks[block_index] = new_block;
        
        // 准备要写入的数据块
        char block_data[BLOCK_SIZE] = {0};
        size_t block_bytes = (size - bytes_written < BLOCK_SIZE) ? (size - bytes_written) : BLOCK_SIZE;
        memcpy(block_data, buffer + bytes_written, block_bytes);
        
        // 写入
        disk_write_block(file_inode->blocks[block_index], block_data);
        
        bytes_written += block_bytes;
        block_index++;
    }
    
    // 更新文件大小
    file_inode->size = bytes_written;
    
    // 写回inode
    _write_inode(inode_num, file_inode);
    
    return bytes_written;
}


// =============================================================================
// 公共API函数 (带锁)
// 
// 这些是暴露给外部（如 main.c）的函数
// 它们负责获取和释放 fs_lock 锁
// =============================================================================

/**
 * 初始化文件系统锁
 * (这个函数应该在 main.c 中，在创建任何线程之前被调用)
 */
void init_fs_lock() {
    pthread_mutex_init(&fs_lock, NULL);
}

/**
 * 格式化磁盘
 */
int format_disk() {
    pthread_mutex_lock(&fs_lock);
    
    // 初始化超级块
    memset(&fs.superblock, 0, sizeof(superblock_t));
    fs.superblock.magic = 0x12345678;
    fs.superblock.blocks = DISK_BLOCKS;
    fs.superblock.inode_blocks = INODE_BLOCKS;
    fs.superblock.data_blocks = DATA_BLOCKS;
    fs.superblock.free_inode_count = MAX_FILES - 1; // 保留根目录inode
    fs.superblock.free_data_count = DATA_BLOCKS - 1; // 保留根目录数据块
    fs.superblock.state = 1; 
    
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
    
    // 写入根目录inode (根目录inode编号为0)
    _write_inode(0, &root_inode);
    
    // 初始化根目录数据块（空目录）
    char root_data[BLOCK_SIZE] = {0};
    disk_write_block(DATA_START_BLOCK, root_data);
    
    printf("磁盘格式化完成\n");
    
    pthread_mutex_unlock(&fs_lock);
    return 0;
}

/**
 * 显示磁盘信息
 */
int show_disk_info() {
    pthread_mutex_lock(&fs_lock);
    
    printf("\n磁盘信息:\n");
    printf("  文件系统类型: SimpleFS v1.0\n");
    printf("  总块数: %u\n", fs.superblock.blocks);
    printf("  Inode区块数: %u\n", fs.superblock.inode_blocks);
    printf("  数据区块数: %u\n", fs.superblock.data_blocks);
    printf("  空闲Inode数: %u\n", fs.superblock.free_inode_count);
    printf("  空闲数据块数: %u\n", fs.superblock.free_data_count);
    printf("  文件系统状态: %s\n", fs.superblock.state ? "已挂载" : "未挂载");
    printf("\n");
    
    pthread_mutex_unlock(&fs_lock);
    return 0;
}

/**
 * 分配一个inode(不带锁)
 */
int alloc_inode() {
    // 没有空闲inode
    if (fs.superblock.free_inode_count <= 0) {
        return -1; 
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

    // 没有找到空闲inode
    return -1; 
}

/**
 * 释放一个inode (无锁)
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
 * 分配一个数据块 (无锁)
 */
int alloc_block() {
    // 没有空闲数据块
    if (fs.superblock.free_data_count <= 0) {
        return -1; 
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
    
    // 没有找到空闲数据块
    return -1; 
}

/**
 * 释放一个数据块 (无锁)
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
 * 创建文件 (带锁)
 */
int create_file(const char* filename) {
    pthread_mutex_lock(&fs_lock);
    
    // 读取根目录inode
    inode_t root_inode;
    _read_inode(0, &root_inode); // 根目录总是inode 0
    
    // 调用无锁的内部函数
    int result = _create_file(filename, &root_inode);
    
    pthread_mutex_unlock(&fs_lock);
    return result;
}

/**
 * 删除文件 (带锁)
 */
int delete_file(const char* filename) {
    pthread_mutex_lock(&fs_lock);
    
    // 读取根目录inode
    inode_t root_inode;
    _read_inode(0, &root_inode);
    
    // 查找文件inode编号
    int inode_num = _find_file_inode(filename, &root_inode);
    if (inode_num == -1) {
        printf("错误: 文件 '%s' 不存在\n", filename);
        pthread_mutex_unlock(&fs_lock);
        return -1;
    }
    
    // 读取文件inode
    inode_t file_inode;
    _read_inode(inode_num, &file_inode);
    
    // 释放数据块
    for (int i = 0; i < 8 && file_inode.blocks[i] != 0; i++) {
        free_block(file_inode.blocks[i]);
    }
    
    // 释放inode
    free_inode(inode_num);
    
    // 清除目录项
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode.blocks[0], root_data);
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].inode == (uint32_t)inode_num) {
            entries[i].inode = 0;
            memset(entries[i].name, 0, MAX_FILENAME);
            break;
        }
    }
    
    // 写回根目录数据块
    disk_write_block(root_inode.blocks[0], root_data);
    
    printf("文件 '%s' 删除成功\n", filename);
    
    pthread_mutex_unlock(&fs_lock);
    return 0;
}

/**
 * 列出目录内容 (带锁)
 */
int list_directory() {
    pthread_mutex_lock(&fs_lock);
    
    inode_t root_inode;
    _read_inode(0, &root_inode);
    
    char root_data[BLOCK_SIZE];
    disk_read_block(root_inode.blocks[0], root_data);
    
    dir_entry_t* entries = (dir_entry_t*)root_data;
    int entry_count = BLOCK_SIZE / sizeof(dir_entry_t);
    
    printf("目录内容:\n");
    int file_count = 0;
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].inode != 0) {
            inode_t file_inode;
            _read_inode(entries[i].inode, &file_inode);
            
            const char* type_str = (file_inode.type == 1) ? "文件" : (file_inode.type == 2) ? "目录" : "未知";
            printf("  %-20s [%s, 大小: %u 字节]\n", entries[i].name, type_str, file_inode.size);
            file_count++;
        }
    }
    
    if (file_count == 0) {
        printf("  (空目录)\n");
    }
    printf("\n");
    
    pthread_mutex_unlock(&fs_lock);
    return 0;
}

/**
 * 读取文件内容 (带锁)
 */
int read_file(const char* filename, char* buffer, size_t size) {
    pthread_mutex_lock(&fs_lock);
    
    // 读取根目录inode
    inode_t root_inode;
    _read_inode(0, &root_inode);
    
    // 查找文件inode编号
    int inode_num = _find_file_inode(filename, &root_inode);
    if (inode_num == -1) {
        printf("错误: 文件 '%s' 不存在\n", filename);
        pthread_mutex_unlock(&fs_lock);
        return -1;
    }
    
    // 读取文件inode
    inode_t file_inode;
    _read_inode(inode_num, &file_inode);
    
    // 调用内部读取函数
    int bytes_read = _read_file(inode_num, &file_inode, buffer, size);
    
    if (bytes_read == -1) {
         printf("错误: '%s' 不是一个普通文件\n", filename);
    }
    
    pthread_mutex_unlock(&fs_lock);
    return bytes_read;
}

/**
 * 写入文件内容 (带锁)
 */
int write_file(const char* filename, const char* buffer, size_t size) {
    pthread_mutex_lock(&fs_lock);
    
    // 读取根目录inode
    inode_t root_inode;
    _read_inode(0, &root_inode);
    
    // 查找文件inode编号
    int inode_num = _find_file_inode(filename, &root_inode);
    if (inode_num == -1) {
        printf("错误: 文件 '%s' 不存在\n", filename);
        pthread_mutex_unlock(&fs_lock);
        return -1;
    }
    
    // 读取文件inode
    inode_t file_inode;
    _read_inode(inode_num, &file_inode);
    
    // 调用内部写入函数
    int bytes_written = _write_file(inode_num, &file_inode, buffer, size);

    if (bytes_written == -1) {
         printf("错误: '%s' 不是一个普通文件\n", filename);
    } else {
        printf("向文件 '%s' 写入了 %d 字节\n", filename, bytes_written);
    }
    
    pthread_mutex_unlock(&fs_lock);
    return bytes_written;
}

/**
 * 复制文件 (带锁)
 */
int copy_file(const char* src_filename, const char* dest_filename) {
    pthread_mutex_lock(&fs_lock);
    
    int result = -1; // 默认失败
    char* buffer = NULL;

    // 读取根目录inode
    inode_t root_inode;
    _read_inode(0, &root_inode);

    // 检查目标文件是否已存在
    if (_find_file_inode(dest_filename, &root_inode) != -1) {
        printf("错误: 目标文件 '%s' 已存在\n", dest_filename);

        // 跳转到清理步骤
        goto cleanup; 
    }

    // 查找源文件inode
    int src_inode_num = _find_file_inode(src_filename, &root_inode);
    if (src_inode_num == -1) {
        printf("错误: 源文件 '%s' 不存在\n", src_filename);
        goto cleanup;
    }

    // 读取源文件inode
    inode_t src_inode;
    _read_inode(src_inode_num, &src_inode);

    if (src_inode.type != 1) {
        printf("错误: 源 '%s' 不是一个普通文件\n", src_filename);
        goto cleanup;
    }

    // 为源文件内容分配缓冲区
    size_t max_size = BLOCK_SIZE * 8; 
    buffer = (char*)malloc(max_size);
    if (buffer == NULL) {
        printf("错误: 内存分配失败\n");
        goto cleanup;
    }

    // 读取源文件内容
    int bytes_read = _read_file(src_inode_num, &src_inode, buffer, max_size);
    if (bytes_read < 0) {
        printf("错误: 读取源文件 '%s' 失败\n", src_filename);
        goto cleanup;
    }

    // 创建目标文件
    if (_create_file(dest_filename, &root_inode) < 0) {
        goto cleanup;
    }

    // 查找新创建的目标文件的inode
    int dest_inode_num = _find_file_inode(dest_filename, &root_inode);
    if (dest_inode_num == -1) {
        printf("错误: 无法找到新创建的目标文件 '%s'\n", dest_filename);
        goto cleanup;
    }

    // 读取目标文件inode
    inode_t dest_inode;
    _read_inode(dest_inode_num, &dest_inode);

    // 将缓冲区内容写入目标文件
    int bytes_written = _write_file(dest_inode_num, &dest_inode, buffer, bytes_read);
    if (bytes_written != bytes_read) {
        printf("错误: 写入目标文件 '%s' 失败\n", dest_filename);
        // 删除不完整的目标文件
        delete_file(dest_filename);
        goto cleanup;
    }

    printf("文件 '%s' 成功复制到 '%s'\n", src_filename, dest_filename);
    result = 0; 

cleanup:

    if (buffer != NULL) {
        free(buffer);
    }
    pthread_mutex_unlock(&fs_lock);
    return result;
}