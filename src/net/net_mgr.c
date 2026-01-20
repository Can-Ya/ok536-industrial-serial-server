#include "net_mgr.h"
#include "../log/log.h"


/**
 * Initialize TCP client structure
 * @param client: Pointer to TcpClient structure
 */
static void tcp_client_init(TcpClient* client) {
    if (!client) return;
    memset(client, 0, sizeof(TcpClient));
    client->fd = -1;
    client->connected = 0;
    client->last_active = time(NULL);
    pthread_mutex_init(&client->mutex, NULL);
}

/**
 * Destroy TCP client structure and release resources
 * @param client: Pointer to TcpClient structure
 */
static void tcp_client_destroy(TcpClient* client) {
    if (!client) return;
    if (client->fd > 0) {
        close(client->fd);
    }
    pthread_mutex_destroy(&client->mutex);
}

/**
 * Update TCP client active time
 * @param mgr: Pointer to NetMgr instance
 * @param client_idx: Pointer to client idx
 */
static void update_client_active(NetMgr* mgr, int client_idx)
{
    if (!mgr || client_idx < 0 || client_idx >= MAX_CLIENT_NUM) return;
    TcpClient* client = &mgr->clients[client_idx];
    client->last_active = time(NULL);
}

/**
 * Close TCP client
 * @param mgr: Pointer to NetMgr instance
 * @param client_idx: Pointer to client idx
 */
static void close_tcp_client(NetMgr* mgr, int client_idx)
{
    if (!mgr || client_idx < 0 || client_idx >= MAX_CLIENT_NUM) return;
    TcpClient* client = &mgr->clients[client_idx];

    // pthread_mutex_lock(&client->mutex);
    if (client->connected && client->fd > 0) {
        shutdown(client->fd, SHUT_RDWR);
        close(client->fd);
        LOG_INFO("%d (%s:%d) closed (timeout/invalid)", 
                client_idx, inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port));
    }
    client->fd = -1;
    client->connected = 0;
    client->rx_bytes = 0;
    client->tx_bytes = 0;
    client->last_active = 0;
    // pthread_mutex_unlock(&client->mutex);
}

/**
 * TCP client connect clean thread function
 * @param arg: Pointer to NetMgr instance
 * @return NULL on exit
 */
static void* tcp_conn_clean_thread(void* arg)
{
    NetMgr* mgr = (NetMgr*)arg;
    time_t now;

    while (1) {
        now = time(NULL);
        pthread_mutex_lock(&mgr->mutex);

        for (int i = 0; i < MAX_CLIENT_NUM; i++) {
            TcpClient* client = &mgr->clients[i];
            if (!client->connected || client->fd < 0) continue;

            if (difftime(now, client->last_active) > CONN_TIMEOUT) {
                close_tcp_client(mgr, i);
            }
        }

        pthread_mutex_unlock(&mgr->mutex);
        sleep(5);
    }
    return NULL;
}

/**
 * TCP server thread function (handle client connections)
 * @param arg: Pointer to NetMgr instance
 * @return NULL on exit
 */
static void* tcp_server_thread(void* arg) 
{
    NetMgr* mgr = (NetMgr*)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    LOG_INFO("TCP server thread start, listen port: %d", TCP_PORT);

    while (1) {
        int client_fd = accept(mgr->server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("TCP accept failed");
            break;
        }

        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

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
            LOG_WARN("TCP client max num reacher, reject new connection");
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
        client->last_active = time(NULL);
        pthread_mutex_unlock(&client->mutex);

        LOG_INFO("TCP client connected: %s:%d (idx: %d)", 
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_idx);
    }
    return NULL;
}

/**
 * TCP client thread function (connect to server)
 * @param arg: Pointer to NetMgr instance
 * @return NULL on exit
 */
static void* tcp_client_thread(void* arg)
{
    NetMgr* mgr = (NetMgr*)arg;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);

    if (inet_pton(AF_INET, (char*)arg + sizeof(NetMgr*), &server_addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid server IP");
        return NULL;
    }

    while (1) {
        pthread_mutex_lock(&mgr->mutex);
        if (mgr->client_fd < 0 || !mgr->client_fd) {
            mgr->client_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (mgr->client_fd < 0) {
                LOG_ERROR("TCP client socket create failed");
                pthread_mutex_unlock(&mgr->mutex);
                sleep(1);
                continue;
            }

            LOG_INFO("TCP client connecting to %s:%d...", 
                    inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

            if (connect(mgr->client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                LOG_ERROR("TCP client connerc failed");
                close(mgr->client_fd);
                mgr->client_fd = -1;
                pthread_mutex_unlock(&mgr->mutex);
                sleep(3);
                continue;
            }
            LOG_INFO("TCP client connected to server");
        }
        pthread_mutex_unlock(&mgr->mutex);
        sleep(1);
    }
    return NULL;
}

/**
 * UDP thread function (reserved for extension)
 * @param arg: Pointer to NetMgr instance
 * @return NULL on exit
 */
static void* udp_thread(void* arg) {
    // temple NULL
    return NULL;
}

/**
 * Initialize network manager
 * @param mode: Network mode (TCP_SERVER/TCP_CLIENT/UDP)
 * @param server_ip: Server IP (only for TCP_CLIENT mode)
 * @param port: Network port (0 for default port)
 * @return Pointer to NetMgr instance on success, NULL on failure
 */
NetMgr* net_mgr_init(NetMode mode, const char* server_ip, int port) {
    NetMgr* mgr = (NetMgr*)malloc(sizeof(NetMgr));
    if (!mgr) {
        LOG_ERROR("Malloc NetMgr failed");
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
                LOG_ERROR("TCP server socket create failed");
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
                LOG_ERROR("TCP server bind failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }

            if (listen(mgr->server_fd, LISTEN_BACKLOG) < 0) {
                LOG_ERROR("TCP server listen failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }

            if (pthread_create(&mgr->net_thread, NULL, tcp_server_thread, mgr) != 0) {
                LOG_ERROR("Create TCP server thread failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }

             if (pthread_create(&mgr->clean_thread, NULL, tcp_conn_clean_thread, mgr) != 0) {
                LOG_ERROR("Create TCP clean thread failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }
            pthread_detach(mgr->clean_thread);
            break;

        case NET_MODE_TCP_CLIENT: 
            if (pthread_create(&mgr->net_thread, NULL, tcp_client_thread, mgr) != 0) {
                LOG_ERROR("Create TCP client thread failed");
                free(mgr);
                return NULL;
            }
            break;

        case NET_MODE_UDP: 
            mgr->server_fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (mgr->server_fd < 0) {
                LOG_ERROR("UDP socket create failed");
                free(mgr);
                return NULL;
            }
            setsockopt(mgr->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            struct sockaddr_in udp_addr;
            memset(&udp_addr, 0, sizeof(udp_addr));
            udp_addr.sin_family = AF_INET;
            udp_addr.sin_addr.s_addr = INADDR_ANY;
            udp_addr.sin_port = htons(port > 0 ? port : UDP_PORT);

            if (bind(mgr->server_fd, (struct sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
                LOG_ERROR("UDP bind failed");
                close(mgr->server_fd);
                free(mgr);
                return NULL;
            }

            if (pthread_create(&mgr->net_thread, NULL, udp_thread, mgr) != 0) {
                LOG_ERROR("Create UDP thread failed");
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

/**
 * Destroy network manager and release resources
 * @param mgr: Pointer to NetMgr instance
 */
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

    if (mgr->clean_thread) {
        pthread_cancel(mgr->clean_thread);
        pthread_join(mgr->clean_thread, NULL);
    }

    LOG_INFO("NetMge has destroyed");

    pthread_mutex_destroy(&mgr->mutex);
    free(mgr);
}

/**
 * Broadcast TCP data to all connected clients
 * @param mgr: Pointer to NetMgr instance
 * @param data: Data buffer to send
 * @param len: Length of data buffer
 * @return Number of clients successfully sent to
 */
int net_mgr_broadcast_tcp(NetMgr* mgr, const uint8_t* data, int len)
{
    if (!mgr || !data || len <= 0) return -1;

    int send_count = 0;
    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < MAX_CLIENT_NUM; i++) {
        TcpClient* client = &mgr->clients[i];
        if (!client->connected || client->fd < 0) continue;

        pthread_mutex_lock(&client->mutex);
        ssize_t ret = send(client->fd, (const void*)data, len, MSG_NOSIGNAL);
        if (ret > 0) {
            client->tx_bytes += ret;
            update_client_active(mgr, i);
            send_count++;
        } else if (ret < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("Send to client %d failed, close conn", i);
                close_tcp_client(mgr, i);
            }
        }
        pthread_mutex_unlock(&client->mutex);
    }
    pthread_mutex_unlock(&mgr->mutex);

    return send_count;
}

/**
 * Send TCP data to specified client
 * @param mgr: Pointer to NetMgr instance
 * @param client_idx: Client index (0 ~ MAX_CLIENT_NUM-1)
 * @param data: Data buffer to send
 * @param len: Length of data buffer
 * @return Number of bytes sent on success, -1 on failure
 */
int net_mgr_send_tcp(NetMgr* mgr, int client_idx, const uint8_t* data, int len) {
    if (!mgr || client_idx < 0 || client_idx >= MAX_CLIENT_NUM || !data || len <= 0) {
        return -1;
    }

    pthread_mutex_lock(&mgr->mutex);

    TcpClient* client = &mgr->clients[client_idx];
    pthread_mutex_lock(&client->mutex);
    if (!client->connected || client->fd < 0) {
        pthread_mutex_unlock(&client->mutex);
        pthread_mutex_unlock(&mgr->mutex);
        return -1;
    }

    ssize_t ret = send(client->fd, (const void*)data, len, MSG_NOSIGNAL);
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

/**
 * Receive TCP data from specified client
 * @param mgr: Pointer to NetMgr instance
 * @param client_idx: Client index (0 ~ MAX_CLIENT_NUM-1)
 * @param buf: Output data buffer
 * @param len: Length of output buffer
 * @return Number of bytes received on success, 0 on timeout, -1 on failure
 */
int net_mgr_recv_tcp(NetMgr* mgr, int client_idx, uint8_t* buf, int len)
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

    struct timeval tv = {1, 0};
    setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t ret = recv(client->fd, (void*)buf, len - 1, MSG_NOSIGNAL);
    if (ret > 0) {
        client->rx_bytes += ret;
        update_client_active(mgr, client_idx);
        buf[ret] = '\0';
    } else if (ret == 0) {
        close_tcp_client(mgr, client_idx);
        ret = -1; 
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ret = 0; 
    } else {
        close_tcp_client(mgr, client_idx);
        ret = -1;
    }

    pthread_mutex_unlock(&client->mutex);

    return ret;
}

/**
 * Send UDP data to specified IP/port
 * @param mgr: Pointer to NetMgr instance
 * @param ip: Destination IP address
 * @param port: Destination port
 * @param data: Data buffer to send
 * @param len: Length of data buffer
 * @return Number of bytes sent on success, -1 on failure
 */
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

/**
 * Receive UDP data and get source IP/port
 * @param mgr: Pointer to NetMgr instance
 * @param buf: Output data buffer
 * @param len: Length of output buffer
 * @param src_ip: Output source IP address
 * @param src_port: Output source port
 * @return Number of bytes received on success, -1 on failure
 */
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

