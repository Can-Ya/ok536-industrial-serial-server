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
#include "modbus_core.h"

// Global constants for UART management
#define MAX_UART_NUM 17          // Maximum number of UART devices supported
#define BUF_SIZE 1024            // Default buffer size for UART data transmission/reception
#define EPOLL_MAX_EVENTS 32      // Max epoll events to monitor for UART read/write

// Configuration structure for UART device parameters (parsed from YAML config file)
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

// Runtime status structure for a single UART device
typedef struct {
    int fd;
    UartConfig config;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t err_count;
} UartDev;

// Manager structure for global UART device management
typedef struct {
    UartDev uarts[MAX_UART_NUM];
    int epoll_fd;
    int uart_count;
} UartMgr;

UartMgr* uart_mgr_init(const char* config_path);

void uart_mgr_destroy(UartMgr* mgr);

int uart_mgr_write(UartMgr* mgr, int uart_idx, const char* data, int len);

void uart_mgr_get_status(UartMgr* mgr, int uart_idx, UartDev* status);

UartDev* uart_mgr_get_uart_by_idx(UartMgr* mgr, int uart_idx);

int modbus_rtu_frame_write(UartMgr* mgr, int uart_idx, const ModbusRTUFrame* rtu_frame);

#endif // !UART_MGR_H

