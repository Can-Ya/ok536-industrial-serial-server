#ifndef UART_MGR_H
#define UART_MGR_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/epoll.h>
#include <string.h>
#include <yaml.h>

#define MAX_UART_NUM 17
#define BUF_SIZE 102
#define EPOLL_MAX_EVENTS 32

typedef struct {
    int idx;
    char dev_path[64];
    int baudrate;
    int databit;
    int stopbit;
    char parity;
    int flow_ctrl;
    int enable;
    int modbus_enable;
} UartConfig;

typedef struct {
    int fd;
    UartConfig config;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t err_count;
} UartDev;

typedef struct {
    UartDev uarts[MAX_UART_NUM];
    int epoll_fd;
    int uart_count;
} UartMgr;

UartMgr* uart_mgr_init(const char* config_path);

void uart_mgr_destroy(UartMgr* mgr);

void uart_mgr_event_loop(UartMgr* mgr);

int uart_mgr_write(UartMgr* mgr, int uart_idx, const char* data, int len);

void uart_mgr_get_status(UartMgr* mgr, int uart_idx, UartDev* status);


#endif // !UART_MGR_H

