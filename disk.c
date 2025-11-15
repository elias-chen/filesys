#include "disk.h"

filesystem_t fs = {0};

/**
 * 初始化磁盘系统
 */
int disk_init(const char* filename) {
    fs.file = fopen(filename, "rb+");
    if (!fs.file) {
        // 如果文件不存在，则创建新文件
        fs.file = fopen(filename, "wb+");
        if (!fs.file) {
            return -1;
        }
        
        // 扩展文件到所需大小
        fseek(fs.file, DISK_SIZE - 1, SEEK_SET);
        fputc(0, fs.file);
        fseek(fs.file, 0, SEEK_SET);
    }
    
    // 读取超级块
    disk_read_block(SUPERBLOCK_BLOCK, &fs.superblock);
    
    // 如果是第一次初始化或者魔数不正确，则需要格式化
    if (fs.superblock.magic != 0x12345678) {
        printf("检测到未初始化的磁盘，请执行 format 命令来手动初始化...\n");
    } else {
        // 读取inode位图
        disk_read_block(INODE_BITMAP_BLOCK, fs.inode_bitmap);
        
        // 读取数据块位图
        disk_read_block(DATA_BITMAP_BLOCK, fs.data_bitmap);
    }
    
    return 0;
}

/**
 * 关闭磁盘系统
 */
void disk_close() {
    if (fs.file) {
        fclose(fs.file);
        fs.file = NULL;
    }
}

/**
 * 读取指定块
 */
int disk_read_block(uint32_t block_num, void* buffer) {
    if (block_num >= DISK_BLOCKS || !buffer) {
        return -1;
    }
    
    fseek(fs.file, block_num * BLOCK_SIZE, SEEK_SET);
    fread(buffer, BLOCK_SIZE, 1, fs.file);
    return 0;
}

/**
 * 写入指定块
 */
int disk_write_block(uint32_t block_num, const void* buffer) {
    if (block_num >= DISK_BLOCKS || !buffer) {
        return -1;
    }
    
    fseek(fs.file, block_num * BLOCK_SIZE, SEEK_SET);
    fwrite(buffer, BLOCK_SIZE, 1, fs.file);
    fflush(fs.file);
    return 0;
}