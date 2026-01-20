#ifndef NET_MGR_H
#define NET_MGR_H

#include <stdio.h>
#include <stdint.h> 
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

// Global constants for network management
#define TCP_PORT 8888
#define UDP_PORT 8889
#define MAX_CLIENT_NUM 4
#define LISTEN_BACKLOG 5
#define BUF_SIZE 1024
#define CONN_TIMEOUT 30

// Network working mode enumeration
typedef enum {
    NET_MODE_TCP_SERVER,
    NET_MODE_TCP_CLIENT,
    NET_MODE_UDP
} NetMode;

// Runtime status structure for a single TCP client
typedef struct {
    int fd;
    struct sockaddr_in addr;
    int connected;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    pthread_mutex_t mutex;
    time_t last_active;
} TcpClient;

// Manager structure for global network resource management
typedef struct {
    NetMode mode;
    int server_fd;
    int client_fd;
    TcpClient clients[MAX_CLIENT_NUM];
    pthread_t net_thread;
    pthread_t clean_thread;
    pthread_mutex_t mutex;
} NetMgr;

NetMgr* net_mgr_init(NetMode mode, const char* server_ip, int port);

void net_mgr_destroy(NetMgr* mgr);

int net_mgr_broadcast_tcp(NetMgr* mgr, const uint8_t* data, int len);

int net_mgr_send_tcp(NetMgr* mgr, int client_idx, const uint8_t* data, int len);

int net_mgr_recv_tcp(NetMgr* mgr, int client_idx, uint8_t* buf, int len);

int net_mgr_send_udp(NetMgr* mgr, const char* ip, int port, const char* data, int len);

int net_mgr_recv_udp(NetMgr* mgr, char* buf, int len, char* src_ip, int* src_port);

#endif // !NET_MGR_H
