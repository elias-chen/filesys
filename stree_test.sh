#!/bin/bash

# --- 配置 ---
EXECUTABLE="./filesystem"       # 你的可执行文件
LOG_FILE="stress_test_run.log"  # 存储所有命令输出的日志
RESOURCE_LOG="resource_usage.csv" # 存储CPU/内存/磁盘占用的CSV日志
DURATION_HOURS= 1               # 测试时长（小时）
# ---

# 转换时长为秒
DURATION_SECONDS=$(( 120 ))

# 步骤 1: 确保可执行文件存在
# --------------------------------
echo "--- 步骤 1: 检查并编译程序 ---"
if [ ! -f "$EXECUTABLE" ]; then
    echo "未找到可执行文件 '$EXECUTABLE'。正在运行 'make'..."
    if ! make; then
        echo "编译失败！请检查 Makefile 和代码。"
        exit 1
    fi
    echo "编译成功。"
else
    echo "找到可执行文件 '$EXECUTABLE'。"
fi


# 步骤 2: 确保磁盘映像存在并已格式化
# --------------------------------
if [ ! -f "disk.img" ]; then
    echo "--- 步骤 2: 未找到 'disk.img'，正在格式化... ---"
    # 使用 'echo' 通过管道发送 "format" 和 "exit" 命令
    echo -e "format\nexit\n" | $EXECUTABLE > /dev/null
    echo "磁盘 'disk.img' 已创建并格式化。"
else
    echo "--- 步骤 2: 找到 'disk.img' ---"
fi


# 步骤 3: 定义资源监控函数 (将在后台运行)
# --------------------------------
monitor_resources() {
    echo "资源监控器已启动... (PID: $$)"
    # 写入 CSV 头部
    echo "Timestamp,CPU_%,Mem_%,RSS_KB,Disk_IMG_KB,PID" > $RESOURCE_LOG

    # 循环，直到主测试进程停止
    # 我们通过检查 $1 (父进程PID) 是否还活着
    while kill -0 $1 2>/dev/null; do
        # 查找 filesystem 进程的 PID
        FS_PID=$(pgrep -f $EXECUTABLE)
        
        if [ -n "$FS_PID" ]; then
            # 使用 ps 获取 CPU, 内存百分比, 和 RSS (实际使用内存)
            STATS=$(ps -p $FS_PID -o %cpu,%mem,rss --no-headers | awk '{$1=$1;print}')
            if [ -n "$STATS" ]; then
                CPU=$(echo $STATS | awk '{print $1}')
                MEM=$(echo $STATS | awk '{print $2}')
                RSS=$(echo $STATS | awk '{print $3}')
            else
                # 进程可能瞬间消失
                CPU="0"; MEM="0"; RSS="0"
            fi
        else
            FS_PID="N/A"; CPU="0"; MEM="0"; RSS="0"
        fi
        
        # 获取 disk.img 的大小 (KB)
        DISK_IMG_SIZE=$(du -k disk.img | cut -f1)
        
        # 记录数据
        echo "$(date +'%Y-%m-%d %H:%M:%S'),$CPU,$MEM,$RSS,$DISK_IMG_SIZE,$FS_PID" >> $RESOURCE_LOG
        
        # 每 10 秒记录一次
        sleep 10
    done
    echo "资源监控器正在退出。"
}


# 步骤 4: 定义命令生成器函数
# --------------------------------
# 这个函数会持续生成命令，直到12小时结束
run_test_commands() {
    echo "命令生成器已启动，将运行 $DURATION_HOURS 小时..."
    
    # 计算结束时间
    END_TIME=$(( $(date +%s) + $DURATION_SECONDS ))
    
    CURRENT_TIME=$(date +%s)
    
    # 循环直到超时
    while [ $CURRENT_TIME -lt $END_TIME ]; do
        
        # 'test' 命令会执行50个文件的 C-W-R-D 操作
        echo "test"
        
        # 'ls' 和 'df' 增加多样性
        echo "ls"
        echo "df"
        
        # 可以在这里加一个短暂的 sleep 来模拟用户间隔
        # sleep 0.1
        
        # 更新当前时间
        CURRENT_TIME=$(date +%s)
    done
    
    # 12小时结束后，发送 'exit' 命令来终止 filesystem 程序
    echo "exit"
    echo "命令生成器已完成。"
}


# 步骤 5: 启动测试
# --------------------------------
echo "--- 步骤 3: 启动 $DURATION_HOURS 小时压力测试 ---"
echo "所有输出将重定向到: $LOG_FILE"
echo "资源使用情况将记录到: $RESOURCE_LOG"
echo "测试正在进行中，请耐心等待..."

# 启动资源监控器 (在后台)
# $$ 返回当前脚本（父进程）的PID
monitor_resources $$ &
MONITOR_PID=$!

# 启动命令生成器，并将其输出通过管道 (pipe)
# 作为输入发送给你的 filesystem 程序。
# filesystem 的所有标准输出和错误都重定向到 $LOG_FILE
run_test_commands | $EXECUTABLE > $LOG_FILE 2>&1

# 捕获 filesystem 程序的退出状态
TEST_EXIT_CODE=$?


# 步骤 6: 清理和报告
# --------------------------------
echo "--- 步骤 4: 测试完成 ---"
echo "Filesystem 进程退出码: $TEST_EXIT_CODE"

# 停止资源监控器
echo "正在停止资源监控器 (PID: $MONITOR_PID)..."
if kill $MONITOR_PID 2>/dev/null; then
    echo "监控器已停止。"
else
    echo "监控器已自行退出。"
fi

echo "测试日志已保存到: $LOG_FILE"
echo "资源日志已保存到: $RESOURCE_LOG"
echo "你可以使用电子表格软件打开 '$RESOURCE_LOG' 来分析CPU和内存使用情况。"
echo "测试完成。"