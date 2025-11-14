#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <pthread.h> // 导入 POSIX 线程库
#include <unistd.h>  // for sleep (可选)

#include "disk.h"
#include "file_ops.h"

// =============================================================================
// 任务队列 (生产者-消费者模型)
// =============================================================================

#define MAX_TASKS 10 // 任务队列最大容量
#define MAX_ARG_LEN 256 // 参数最大长度
#define NUM_CONSUMERS 4 // 消费者线程数量 (可根据需求调整)

// 命令类型枚举
typedef enum {
    CMD_HELP,
    CMD_FORMAT,
    CMD_DF,
    CMD_TOUCH,
    CMD_RM,
    CMD_LS,
    CMD_CAT,
    CMD_ECHO,
    CMD_COPY,
    CMD_TEST, // 压力测试命令
    CMD_UNKNOWN
} command_e;

// 任务结构体
typedef struct {
    command_e cmd;
    char arg1[MAX_ARG_LEN];
    char arg2[MAX_ARG_LEN];
    char content[1024]; // 用于 'echo' 命令的内容
} task_t;

// 全局任务队列
task_t task_queue[MAX_TASKS];
int task_count = 0;
int task_head = 0;
int task_tail = 0;

// 队列同步原语
pthread_mutex_t queue_lock;      // 保护任务队列的互斥锁
pthread_cond_t queue_not_empty; // 条件变量: 通知消费者队列中有任务
pthread_cond_t queue_not_full;  // 条件变量: 通知生产者队列未满

// 线程管理
pthread_t consumer_threads[NUM_CONSUMERS];
volatile int running = 1; // 控制所有线程运行的全局标志

// =============================================================================
// 消费者线程
// =============================================================================

/**
 * @brief 消费者线程的执行函数。
 * * 这是一个无限循环，不断尝试从任务队列中获取任务并执行。
 * "执行" 任务意味着调用 file_ops.c 中对应的线程安全函数。
 *
 * @param arg 未使用
 * @return void* */
void* consumer_thread(void* arg) {
    (void)arg; // 避免 "unused parameter" 警告
    
    long thread_id = (long)pthread_self(); // 获取唯一的线程ID
    printf("[线程 %ld] 启动成功。\n", thread_id);

    while (running) {
        // 1. ---- 获取任务 (受队列锁保护) ----
        pthread_mutex_lock(&queue_lock);
        
        // 当队列为空且程序仍在运行时，等待
        while (running && task_count == 0) {
            // pthread_cond_wait 会自动释放 queue_lock，
            // 并在被唤醒时重新获取它。
            pthread_cond_wait(&queue_not_empty, &queue_lock);
        }
        
        // 如果程序停止运行，立即退出
        if (!running) {
            pthread_mutex_unlock(&queue_lock);
            break;
        }

        // 从队列中取出一个任务
        task_t task = task_queue[task_head];
        task_head = (task_head + 1) % MAX_TASKS;
        task_count--;

        // 通知生产者队列未满
        pthread_cond_signal(&queue_not_full);
        
        // 释放队列锁，让其他线程可以访问队列
        pthread_mutex_unlock(&queue_lock);
        
        // 2. ---- 执行任务 (不受队列锁保护) ----
        // 真正的文件系统操作由 file_ops.c 中的 fs_lock 保护
        // printf("\n[线程 %ld] 正在处理任务...\n", thread_id);
        
        switch (task.cmd) {
            case CMD_HELP:
                print_help();
                break;
            case CMD_FORMAT:
                format_disk();
                break;
            case CMD_DF:
                show_disk_info();
                break;
            case CMD_TOUCH:
                create_file(task.arg1);
                break;
            case CMD_RM:
                delete_file(task.arg1);
                break;
            case CMD_LS:
                list_directory();
                break;
            case CMD_CAT: {
                char buffer[BLOCK_SIZE * 8] = {0}; // 最多读取8个块
                int bytes_read = read_file(task.arg1, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    printf("%s\n", buffer);
                }
                break;
            }
            case CMD_ECHO:
                write_file(task.arg1, task.content, strlen(task.content));
                break;
            case CMD_COPY:
                copy_file(task.arg1, task.arg2);
                break;
            case CMD_TEST:
                // 压力测试任务: 创建 -> 写入 -> 读取 -> 删除
                printf("[测试线程 %ld] 正在测试 %s\n", thread_id, task.arg1);
                if (create_file(task.arg1) == 0) {
                    write_file(task.arg1, "pressure test data", 18);
                    char test_buf[100];
                    read_file(task.arg1, test_buf, 99);
                    delete_file(task.arg1);
                }
                break;
            case CMD_UNKNOWN:
            default:
                printf("[线程 %ld] 收到一个未知任务。\n", thread_id);
                break;
        }
        // 重新打印提示符，模仿shell的行为
        printf("fs> ");
        fflush(stdout);
    }
    
    printf("[线程 %ld] 正在退出。\n", thread_id);
    return NULL;
}

// =============================================================================
// 生产者 (主线程)
// =============================================================================

/**
 * @brief (生产者) 将任务添加到队列
 */
void enqueue_task(task_t task) {
    pthread_mutex_lock(&queue_lock);
    
    // 等待队列未满
    while (task_count == MAX_TASKS) {
        printf("任务队列已满，请稍候...\n");
        // 等待消费者取走任务
        pthread_cond_wait(&queue_not_full, &queue_lock);
    }

    // 入队
    task_queue[task_tail] = task;
    task_tail = (task_tail + 1) % MAX_TASKS;
    task_count++;

    // 通知一个正在等待的消费者
    pthread_cond_signal(&queue_not_empty);
    
    pthread_mutex_unlock(&queue_lock);
}

/**
 * @brief 帮助信息
 */
void print_help() {
    printf("\n文件系统模拟器命令 (多线程版):\n");
    printf("  help            - 显示帮助信息\n");
    printf("  format          - 格式化磁盘\n");
    printf("  df              - 显示磁盘信息\n");
    printf("  touch <name>    - 创建文件\n");
    printf("  rm <name>       - 删除文件\n");
    printf("  ls              - 列出目录内容\n");
    printf("  cat <name>      - 读取文件内容\n");
    printf("  echo <name>     - 写入文件内容\n");
    printf("  copy <src> <dest> - 复制文件\n");
    printf("  test            - 运行50个文件的压力测试\n");
    printf("  exit            - 退出程序\n\n");
}

/**
 * @brief 启动压力测试
 * * 快速向队列中添加50个测试任务。
 * 这将模拟高并发的文件操作请求。
 */
void start_pressure_test() {
    printf("开始压力测试... (向队列提交 50 个文件任务)\n");
    
    for (int i = 0; i < 50; i++) {
        task_t task = {0};
        task.cmd = CMD_TEST;
        // 使用 snprintf 安全地格式化文件名
        snprintf(task.arg1, MAX_ARG_LEN, "testfile_%d.txt", i);
        enqueue_task(task);
    }
    
    printf("50个测试任务已全部入队。消费者线程将开始处理。\n");
}

/**
 * @brief 主函数 (生产者)
 */
int main() {
    
    printf("用户态文件系统模拟器 (多线程版)\n");
    printf("=================================\n");
    
    // 初始化磁盘
    if (disk_init("disk.img") < 0) {
        printf("错误: 无法初始化磁盘\n");
        return 1;
    }
    
    // 1. 初始化所有锁和条件变量
    init_fs_lock(); // 初始化文件系统本身的锁 (来自 file_ops.h)
    pthread_mutex_init(&queue_lock, NULL);
    pthread_cond_init(&queue_not_empty, NULL);
    pthread_cond_init(&queue_not_full, NULL);

    // 2. 创建消费者线程
    running = 1;
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        if (pthread_create(&consumer_threads[i], NULL, consumer_thread, NULL) != 0) {
            perror("无法创建消费者线程");
            return 1;
        }
    }
    printf("已启动 %d 个消费者线程。\n", NUM_CONSUMERS);

    print_help();
    
    char command_line[1024];
    
    // 生产者循环
    while (running) {
        printf("fs> ");
        fflush(stdout);
        
        if (!fgets(command_line, sizeof(command_line), stdin)) {
            break; 
        }
        
        // 移除换行符
        command_line[strcspn(command_line, "\n")] = 0;
        
        if (strlen(command_line) == 0) {
            continue;
        }
        
        char cmd[50];
        char arg1[MAX_ARG_LEN] = {0};
        char arg2[MAX_ARG_LEN] = {0};
        
        // 命令解析
        int nargs = sscanf(command_line, "%s %s %s", cmd, arg1, arg2);
        
        task_t task = {0}; // 初始化任务
        
        // (生产者) - 将命令解析为任务
        if (strcmp(cmd, "help") == 0) {
            task.cmd = CMD_HELP;
        } else if (strcmp(cmd, "format") == 0) {
            task.cmd = CMD_FORMAT;
        } else if (strcmp(cmd, "df") == 0) {
            task.cmd = CMD_DF;
        } else if (strcmp(cmd, "touch") == 0) {
            if (nargs < 2) { printf("用法: touch <文件名>\n"); continue; }
            task.cmd = CMD_TOUCH;
            strncpy(task.arg1, arg1, MAX_ARG_LEN - 1);
        } else if (strcmp(cmd, "rm") == 0) {
            if (nargs < 2) { printf("用法: rm <文件名>\n"); continue; }
            task.cmd = CMD_RM;
            strncpy(task.arg1, arg1, MAX_ARG_LEN - 1);
        } else if (strcmp(cmd, "ls") == 0) {
            task.cmd = CMD_LS;
        } else if (strcmp(cmd, "cat") == 0) {
            if (nargs < 2) { printf("用法: cat <文件名>\n"); continue; }
            task.cmd = CMD_CAT;
            strncpy(task.arg1, arg1, MAX_ARG_LEN - 1);
        } else if (strcmp(cmd, "echo") == 0) {
            if (nargs < 2) { printf("用法: echo <文件名>\n"); continue; }
            task.cmd = CMD_ECHO;
            strncpy(task.arg1, arg1, MAX_ARG_LEN - 1);
            
            // echo 命令需要额外读取内容
            printf("请输入要写入的内容 (输入完成后按Enter):\n");
            char buffer[1024] = {0};
            if (fgets(buffer, sizeof(buffer), stdin)) {
                buffer[strcspn(buffer, "\n")] = 0; // 移除换行符
                strncpy(task.content, buffer, 1023);
            } else {
                continue; // 输入失败
            }
        } else if (strcmp(cmd, "copy") == 0) {
            if (nargs < 3) { printf("用法: copy <源文件> <目标文件>\n"); continue; }
            task.cmd = CMD_COPY;
            strncpy(task.arg1, arg1, MAX_ARG_LEN - 1);
            strncpy(task.arg2, arg2, MAX_ARG_LEN - 1);
        } else if (strcmp(cmd, "test") == 0) {
            // 'test' 命令由主线程直接调用，它会 *生产* 大量任务
            start_pressure_test();
            continue; // 不将 'test' 命令本身入队
        } else if (strcmp(cmd, "exit") == 0) {
            running = 0; // 停止生产者循环
            break;
        } else {
            printf("未知命令: %s\n", cmd);
            print_help();
            continue; // 不入队
        }
        
        // (生产者) - 将有效任务加入队列
        enqueue_task(task);
    }
    
    // 3. 关闭程序
    printf("正在关闭...\n");
    running = 0; // 确保所有循环都能看到退出标志
    
    // 唤醒所有可能在等待的消费者，以便它们检查 running 标志并退出
    pthread_mutex_lock(&queue_lock);
    pthread_cond_broadcast(&queue_not_empty); // 唤醒所有等待 "not_empty" 的
    pthread_cond_broadcast(&queue_not_full);  // 唤醒所有等待 "not_full" 的
    pthread_mutex_unlock(&queue_lock);
    
    // 4. 等待所有消费者线程退出
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumer_threads[i], NULL);
    }
    printf("所有消费者线程已退出。\n");

    // 5. 清理资源
    pthread_mutex_destroy(&queue_lock);
    pthread_cond_destroy(&queue_not_empty);
    pthread_cond_destroy(&queue_not_full);
    
    disk_close();
    printf("再见!\n");
    return 0;
}