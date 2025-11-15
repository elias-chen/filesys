#include <stdio.h>
// #include <stdlib.h>
#include <string.h>
// #include <locale.h>
#include "disk.h"
#include "file_ops.h"

void print_help() {
    printf("\n文件系统模拟器命令:\n");
    printf("  help            - 显示帮助信息\n");
    printf("  format          - 格式化磁盘\n");
    printf("  df              - 显示磁盘信息\n");
    printf("  touch <name>    - 创建文件\n");
    printf("  rm <name>       - 删除文件\n");
    printf("  ls              - 列出目录内容\n");
    printf("  cat <name>      - 读取文件内容\n");
    printf("  echo <name>     - 写入文件内容\n");
    printf("  exit            - 退出程序\n\n");
}

int main() {
    
    printf("用户态文件系统模拟器\n");
    printf("====================\n");
    
    // 初始化磁盘
    if (disk_init("disk.img") < 0) {
        printf("错误: 无法初始化磁盘\n");
        return 1;
    }
    
    char command[256];
    char arg[256];
    
    print_help();
    
    while (1) {
        printf("fs> ");
        fflush(stdout);
        
        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }
        
        // 移除换行符
        command[strcspn(command, "\n")] = 0;
        
        if (strlen(command) == 0) {
            continue;
        }
        
        // 解析命令
        char cmd[50];
        int nargs = sscanf(command, "%s %255[^\n]", cmd, arg);
        
        if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "format") == 0) {
            format_disk();
        } else if (strcmp(cmd, "df") == 0) {
            show_disk_info();
        } else if (strcmp(cmd, "touch") == 0) {
            if (nargs < 2) {
                printf("用法: touch <文件名>\n");
            } else {
                create_file(arg);
            }
        } else if (strcmp(cmd, "rm") == 0) {
            if (nargs < 2) {
                printf("用法: rm <文件名>\n");
            } else {
                delete_file(arg);
            }
        } else if (strcmp(cmd, "ls") == 0) {
            list_directory();
        } else if (strcmp(cmd, "cat") == 0) {
            if (nargs < 2) {
                printf("用法: cat <文件名>\n");
            } else {
                char buffer[BLOCK_SIZE * 8] = {0}; 
                int bytes_read = read_file(arg, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    printf("%s\n", buffer);
                }
            }
        } else if (strcmp(cmd, "echo") == 0) {
            if (nargs < 2) {
                printf("用法: echo <文件名>\n");
            } else {
                printf("请输入要写入的内容 (输入完成后按Enter):\n");

                char buffer[1024] = {0};
                if (fgets(buffer, sizeof(buffer), stdin)) {
                    // 移除末尾的换行符
                    buffer[strcspn(buffer, "\n")] = 0;
                    write_file(arg, buffer, strlen(buffer));
                }
            }
        } else if (strcmp(cmd, "exit") == 0) {
            break;
        } else {
            printf("未知命令: %s\n", cmd);
            print_help();
        }
    }
    
    disk_close();
    printf("再见!\n");
    return 0;
}