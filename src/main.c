#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h> 

#include "./log/log.h"
#include "./modbus/modbus_core.h"
#include "./net/net_mgr.h"
#include "./uart/uart_mgr.h"


// Global manager instances (cross-thread shared)
UartMgr*    g_uart_mgr  = NULL;  // Global UART manager instance (manages all UART devices)
NetMgr*     g_net_mgr   = NULL;  // Global network manager instance (handles TCP/UDP communication)
volatile int g_running   = 1;    // Global flag to control program running state (0: exit)
pthread_t   g_modbus_thread;     // Modbus protocol processing thread ID
ModbusRTUFrame g_modbus_rtu;     // Global Modbus RTU frame (for TCP-RTU conversion)
ModbusTCPFrame g_modbus_tcp;     // Global Modbus TCP frame (for network data parsing)

/**
 * Modbus data process thread (TCP -> RTU conversion & UART write)
 * @param arg: Unused
 * @return NULL on exit
 */
void* modbus_process_thread(void* arg)
{
    uint8_t net_recv_buf[BUF_SIZE];

    while (g_running) {
        memset(net_recv_buf, 0, sizeof(net_recv_buf));
        for(int client_idx=0; client_idx<MAX_CLIENT_NUM; client_idx++)
        {
            ssize_t net_recv_len = net_mgr_recv_tcp(g_net_mgr, client_idx, net_recv_buf, sizeof(net_recv_buf)-1);
            // Modbus TCP data example：00 01 00 00 00 06 03 03 00 00 00 01
            if (net_recv_len <= 0) continue;
            if (modbus_parse_tcp_data(net_recv_buf, net_recv_len, &g_modbus_tcp) != 0) {
                fprintf(stderr, "[ERROR] tcp_client %d send data is error\n", client_idx);
                continue;
            }

            if (modbus_tcp_to_rtu(&g_modbus_tcp, &g_modbus_rtu) != 0) {
                fprintf(stderr, "[ERROR] tcp to rtu failed, client idx: %d\n", client_idx);
                continue;
            }

            UartDev* p_uart = uart_mgr_get_uart_by_idx(g_uart_mgr, g_modbus_rtu.slave_addr);
            if (p_uart == NULL || p_uart->fd < 0 || !p_uart->config.enable) {
               fprintf(stderr, "[WARN] UART %d is unenable\n", g_modbus_rtu.slave_addr);
                continue;
            }

            if (p_uart->config.modbus_enable) {
                if (modbus_rtu_frame_write(g_uart_mgr,g_modbus_rtu.slave_addr, &g_modbus_rtu) <= 0) {
                    fprintf(stderr, "[ERROR] UART %d write failed\n", g_modbus_rtu.slave_addr);
                }
            } else {
                if (uart_mgr_write(g_uart_mgr, g_modbus_rtu.slave_addr, g_modbus_rtu.data, g_modbus_rtu.data_len) <= 0) {
                    fprintf(stderr, "[ERROR] UART %d write failed\n", g_modbus_rtu.slave_addr);
                }
            }
        }
        usleep(10000);
    }
    printf("[INFO] Modbus process thread exit\n");
    pthread_exit(NULL);
}

/**
 * Signal handler (catch SIGINT to exit program gracefully)
 * @param sig: Signal number
 */
void sig_handler(int sig)
{
    if (sig == SIGINT) {
        printf("\n[INFO] Catch SIGINT, start exit program...\n");
        g_running = 0;
    }
}

/**
 * Handle UART epoll events (read data & convert to TCP frame)
 */
void epoll_handle_uart_events() {
    struct epoll_event events[EPOLL_MAX_EVENTS];
    int nfds = epoll_wait(g_uart_mgr->epoll_fd, events, EPOLL_MAX_EVENTS, 100); 
    if (nfds < 0) {
        if (errno == EINTR) return; 
        perror("[ERROR] epoll_wait failed");
        return;
    } else if (nfds == 0) {
        return; 
    }

    for (int i = 0; i < nfds; i++) {
        int fd = events[i].data.fd;
        int uart_idx = -1;

        for (int j = 0; j < MAX_UART_NUM; j++) {
            if (g_uart_mgr->uarts[j].fd == fd) {
                uart_idx = j;
                break;
            }
        }
        if (uart_idx == -1) {
            fprintf(stderr, "[WARN] Unknown fd %d in epoll\n", fd);
            continue;
        }

        UartDev* uart = &g_uart_mgr->uarts[uart_idx];
        uint8_t buf[BUF_SIZE] = {0};
        ssize_t len = read(fd, buf, sizeof(buf) - 1);
        if (len <= 0) {
            if (len < 0 && errno != EAGAIN) {
                uart->err_count++;
                perror("[ERROR] UART read failed");
            }
            continue;
        }
        uart->rx_bytes += len;
        printf("[INFO] [%s] Recv %ld bytes: ", uart->config.dev_path, len);
        for (int k = 0; k < len; k++) {
            printf("%02X ", buf[k]);
        }
        printf("\n");

        if (uart->config.modbus_enable) {
            static uint8_t send_buf[BUF_SIZE] = {0};
            int total_send_len = 2 + 2 + 2 + len - 2;
            int offset = 0;
            send_buf[offset++] = MODBUS_TCP_TRANS_ID_H;
            send_buf[offset++] = MODBUS_TCP_TRANS_ID_L;
            send_buf[offset++] = (MODBUS_TCP_PROTOCOL_ID >> 8) & 0xFF;
            send_buf[offset++] = MODBUS_TCP_PROTOCOL_ID & 0xFF;
            send_buf[offset++] = ((len - 2) >> 8) & 0xFF;
            send_buf[offset++] = (len - 2) & 0xFF;
            send_buf[offset++] = uart->config.idx;
            send_buf[offset++] = buf[1];  
            memcpy(&send_buf[offset], buf + 2, len - 4);
            offset += len;

            net_mgr_broadcast_tcp(g_net_mgr, (char*)send_buf, offset);            
        } else {
            // Modbus TCP data example：00 01 00 00 00 06 07 03 00 00 00 01
            static uint8_t send_buf[BUF_SIZE] = {0};
            int total_send_len = 2 + 2 + 2 + 1 + 1 + len;
            int offset = 0;
            send_buf[offset++] = 0;
            send_buf[offset++] = 1;
            send_buf[offset++] = 0;
            send_buf[offset++] = 0;
            send_buf[offset++] = 0;
            send_buf[offset++] = len;
            send_buf[offset++] = uart->config.idx;
            send_buf[offset++] = 3;
            memcpy(&send_buf[offset], buf, len);
            offset += len;

            net_mgr_broadcast_tcp(g_net_mgr, (char*)send_buf, offset);
        }
    }
}

/**
 * Main function (initialize modules & run event loop)
 * @param argc: Argument count
 * @param argv: Argument array (argv[1] = UART config path)
 * @return 0 on success, -1 on failure
 */
int main(int argc, char *argv[])
{
    if (log_init() != 0) {
        // 日志初始化失败的降级提示（可选）
        fprintf(stderr, "Log system init failed! Exit...\n");
        return -1;
    }

    if(argc != 2) {
        fprintf(stderr, "[ERROR] Usage: %s <uart_config.yaml path>\n", argv[0]);
        fprintf(stderr, "[INFO] Example: ./serial_server ./uart_config.yaml\n");
        return -1; 
    }

    signal(SIGINT, sig_handler);

    LOG_INFO("[INFO] Start init UART manager...");
    g_uart_mgr = uart_mgr_init(argv[1]);
    if (g_uart_mgr == NULL) {
        LOG_ERROR("[ERROR] UART manager init failed!");
        return -1;
    }
    LOG_INFO("UART manager init OK, enable UART count: %d", g_uart_mgr->uart_count);

    LOG_INFO("Start init Network manager (TCP Server 192.168.1.232:8888)...");
    g_net_mgr = net_mgr_init(NET_MODE_TCP_SERVER, NULL, 8888);
    if(g_net_mgr == NULL)
    {
        LOG_ERROR("Network manager init failed!");
        uart_mgr_destroy(g_uart_mgr);
        return -1;
    }
    LOG_INFO("Network manager init OK");

    LOG_INFO("Start create Modbus process thread...");
    int phread_ret = pthread_create(&g_modbus_thread, NULL, modbus_process_thread, NULL);
    if (phread_ret != 0) {
        LOG_ERROR("Create modbus thread failed:%s", strerror(phread_ret));
        net_mgr_destroy(g_net_mgr);
        uart_mgr_destroy(g_uart_mgr);
        return -1;
    }
    LOG_INFO("All module init complete! System running...");
    LOG_INFO("Press Ctrl+C to exit");

    while (g_running) {
        epoll_handle_uart_events();
        usleep(1000); 
    }

    LOG_INFO("Start release resource...");
    printf("[EXIT] Start release resource...\n");
    pthread_cancel(g_modbus_thread);
    pthread_join(g_modbus_thread, NULL);
    net_mgr_destroy(g_net_mgr);
    uart_mgr_destroy(g_uart_mgr);
    LOG_INFO("All resource released, program exit success!");
    printf("[EXIT] All resource released, program exit success!\n");

    return 0;
}