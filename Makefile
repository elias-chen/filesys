CC = gcc

CFLAGS = -Wall -Wextra -std=c99 -g -pthread
TARGET = filesystem
SRCS = main.c disk.c file_ops.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) -pthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) disk.img

.PHONY: all clean