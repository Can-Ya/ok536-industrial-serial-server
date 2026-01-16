#include "net_mgr.h"
#include <errno.h>

static void tcp_client_init(TcpClient* client) {
    if (!client) return;
    memset(client, 0, sizeof(TcpClient));
    client->fd = -1;
    client->connected = 0;
    pthread_mutex_init(&client->mutex, NULL);
}

static void tcp_client_destroy(TcpClient* client) {
    if (!client) return;
    if (client->fd > 0) {
        close(client->fd);
    }
    pthread_mutex_destroy(&client->mutex);
}

static void* tcp_server_thread(void* arg) {
    NetMgr* mgr = (NetMgr*)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    printf("TCP server thread start, listen port: %d\n", TCP_PORT);

    while (1) {
        int client_fd = accept(mgr->server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("TCP accept failed");
            break;
        }

        int client_idx = -1;
        pthread_mutex_lock(&mgr->mutex);
        for (int i = 0; i < MAX_CLIENT_NUM; i++) {
            if (!mgr->clients[i].connected) {
                client_idx = i;
                break;
            }
        }
        pthread_mutex_unlock(&mgr->mutex);

        if (client_idx == -1) {
            close(client_fd);
            fprintf(stderr, "TCP client max num reacher, reject new connection\n");
            continue;
        }

        TcpClient* client = &mgr->clients[client_idx];
        pthread_mutex_lock(&client->mutex);
        if (client->fd > 0) {
            close(client->fd);
        }
        client->fd = client_fd;
        client->addr = client_addr;
        client->connected = 1;
        client->rx_bytes = 0;
        client->tx_bytes = 0;
        pthread_mutex_unlock(&client->mutex);

        printf("TCP client connected: %s:%d (idx: %d)\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_idx);
    }
    return NULL;
}

static void* tcp_client_thread(void* arg)
{
    NetMgr* mgr = (NetMgr*)arg;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);

    if (inet_pton(AF_INET, (char*)arg + sizeof(NetMgr*), &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP");
        return NULL;
    }

    while (1) {
        pthread_mutex_lock(&mgr->mutex);
        if (mgr->client_fd < 0 || !mgr->client_fd) {
            mgr->client_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (mgr->client_fd < 0) {
                perror("TCP client socket create failed");
                pthread_mutex_unlock(&mgr->mutex);
                sleep(1);
                continue;
            }

            printf("TCP client connecting to %s:%d...\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
            if (connect(mgr->client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("TCP client connerc failed");
                close(mgr->client_fd);
                mgr->client_fd = -1;
                pthread_mutex_unlock(&mgr->mutex);
                sleep(3);
                continue;
            }
            printf("TCP client connected to server\n");
        }
        pthread_mutex_unlock(&mgr->mutex);
        sleep(1);
    }
    return NULL;
}

// UDP线程函数
static void* udp_thread(void* arg) {
    // 暂留空实现，步骤3可扩展
    return NULL;
}

NetMgr* net_mgr_init(NetMode mode, const char* server_ip, int port) {
    NetMgr* mgr = (NetMgr*)malloc(sizeof(NetMgr));
    if (!mgr) {
        perror("Malloc NetMgr failed");
        return NULL;
    }
    memset(mgr, 0, sizeof(NetMgr));
    mgr->mode = mode;
    pthread_mutex_init(&mgr->mutex, NULL);

    for (int i = 0; i < MAX_CLIENT_NUM; i++) {
        tcp_client_init(&mgr->clients[i]);
    }

    int opt = 1;
    switch (mode) {
        case NET_MODE_TCP_SERVER:
            mgr->server_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (mgr->server_fd < 0) {
                perror("TCP server socket create failed");
                free(mgr);
                return NULL;
            }
            setsockopt(mgr->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = INADDR_ANY;
            server_addr.sin_port = htons(port > 0 ? port : TCP_PORT);

            if (bind(mgr->server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("TCP server bind failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }

            if (listen(mgr->server_fd, LISTEN_BACKLOG) < 0) {
                perror("TCP server listen failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }

            if (pthread_create(&mgr->net_thread, NULL, tcp_server_thread, mgr) != 0) {
                perror("Create TCP server thread failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }
            break;

        case NET_MODE_TCP_CLIENT: 
            if (pthread_create(&mgr->net_thread, NULL, tcp_client_thread, mgr) != 0) {
                perror("Create TCP client thread failed");
                free(mgr);
                return NULL;
            }
            break;

        case NET_MODE_UDP: 
            mgr->server_fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (mgr->server_fd < 0) {
                perror("UDP socket create failed");
                free(mgr);
                return NULL;
            }
            setsockopt(mgr->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            // 绑定地址
            struct sockaddr_in udp_addr;
            memset(&udp_addr, 0, sizeof(udp_addr));
            udp_addr.sin_family = AF_INET;
            udp_addr.sin_addr.s_addr = INADDR_ANY;
            udp_addr.sin_port = htons(port > 0 ? port : UDP_PORT);

            if (bind(mgr->server_fd, (struct sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
                perror("UDP bind failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }

            // 创建UDP线程
            if (pthread_create(&mgr->net_thread, NULL, udp_thread, mgr) != 0) {
                perror("Create UDP thread failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }
            break;
        
        default: 
            free(mgr);
            return NULL;
    }

    return mgr;
}

void net_mgr_destroy(NetMgr* mgr) 
{
    if (!mgr) return;

    if (mgr->server_fd > 0) {
        close(mgr->server_fd);
    }
    if (mgr->client_fd > 0) {
        close(mgr->client_fd);
    }

    for (int i = 0; i < MAX_CLIENT_NUM; i++) {
        tcp_client_destroy(&mgr->clients[i]);
    }

    if (mgr->net_thread) {
        pthread_cancel(mgr->net_thread);
        pthread_join(mgr->net_thread, NULL);
    }

    pthread_mutex_destroy(&mgr->mutex);
    free(mgr);
}

int net_mgr_broadcast_tcp(NetMgr* mgr, const char* data, int len)
{
    if (!mgr || !data || len <= 0) return -1;

    int send_count = 0;
    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < MAX_CLIENT_NUM; i++) {
        TcpClient* client = &mgr->clients[i];
        if (!client->connected || client->fd < 0) continue;

        pthread_mutex_lock(&client->mutex);
        ssize_t ret = send(client->fd, data, len, 0);
        if (ret > 0) {
            client->tx_bytes += ret;
            send_count++;
        } else {
            close(client->fd);
            client->fd = -1;
            client->connected = 0;
        }
        pthread_mutex_unlock(&client->mutex);
    }
    pthread_mutex_unlock(&mgr->mutex);

    return send_count;
}

int net_mgr_send_tcp(NetMgr* mgr, int client_idx, const char* data, int len) {
    if (!mgr || client_idx < 0 || client_idx >= MAX_CLIENT_NUM || !data || len <= 0) {
        return -1;
    }

    pthread_mutex_lock(&mgr->mutex);

    TcpClient* client = &mgr->clients[client_idx];
    pthread_mutex_lock(&client->mutex);
    if (!client->connected || client->fd < 0) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }

    ssize_t ret = send(client->fd, data, len, 0);
    if (ret > 0) {
        client->tx_bytes += ret;
    } else {
        close(client->fd);
        client->fd = -1;
        client->connected = 0;
    }
    pthread_mutex_unlock(&client->mutex);
    pthread_mutex_unlock(&mgr->mutex);

    return ret;
}

int net_mgr_recv_tcp(NetMgr* mgr, int client_idx, char* buf, int len)
{
    if (!mgr || client_idx < 0 || client_idx >= MAX_CLIENT_NUM || !buf || len <= 0) {
        return -1;
    }

    TcpClient* client = &mgr->clients[client_idx];
    pthread_mutex_lock(&client->mutex);
    if (!client->connected || client->fd < 0) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }

    struct timeval tv = {2, 0};
    setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t ret = recv(client->fd, buf, len - 1, 0);
    if (ret > 0) {
        client->rx_bytes += ret;
        buf[ret] = '\0';
    } else if (ret == 0) {
        close(client->fd);
        client->fd = -1;
        client->connected = 0;
        ret = -1; 
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ret = 0; 
    } else {
        close(client->fd);
        client->fd = -1;
        client->connected = 0;
        ret = -1;
    }
    pthread_mutex_unlock(&client->mutex);

    return ret;
}

int net_mgr_send_udp(NetMgr* mgr, const char* ip, int port, const char* data, int len) {
    if (!mgr || mgr->mode != NET_MODE_UDP || !ip || port <= 0 || !data || len <= 0) {
        return -1;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &dest_addr.sin_addr) <= 0) {
        return -1;
    }

    ssize_t ret = sendto(mgr->server_fd, data, len, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    return ret;
}

int net_mgr_recv_udp(NetMgr* mgr, char* buf, int len, char* src_ip, int* src_port) {
    if (!mgr || mgr->mode != NET_MODE_UDP || !buf || len <= 0 || !src_ip || !src_port) {
        return -1;
    }

    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);
    ssize_t ret = recvfrom(mgr->server_fd, buf, len - 1, 0, (struct sockaddr*)&src_addr, &src_len);
    if (ret > 0) {
        buf[ret] = '\0';
        strcpy(src_ip, inet_ntoa(src_addr.sin_addr));
        *src_port = ntohs(src_addr.sin_port);
    }

    return ret;
}

