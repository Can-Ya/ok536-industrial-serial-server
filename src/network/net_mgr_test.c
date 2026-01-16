#include "net_mgr.h"

int main() {
    // 初始化TCP服务器模式
    NetMgr* mgr = net_mgr_init(NET_MODE_TCP_SERVER, NULL, 8888);
    if (!mgr) {
        fprintf(stderr, "NetMgr init failed\n");
        return -1;
    }
    printf("TCP server started, wait for clients...\n");

    // 等待客户端连接，然后广播数据
    sleep(5); // 等待5秒，手动连接客户端（如用telnet 127.0.0.1 8888）

    // 广播测试数据
    char send_data[] = "Hello from TCP server (broadcast)";
    int ret = net_mgr_broadcast_tcp(mgr, send_data, strlen(send_data));
    printf("Broadcast to %d clients: %s\n", ret, send_data);

    // 接收客户端数据（测试第一个客户端）
    char recv_buf[BUF_SIZE] = {0};
    ret = net_mgr_recv_tcp(mgr, 0, recv_buf, sizeof(recv_buf));
    if (ret > 0) {
        printf("Recv from client 0: %s\n", recv_buf);
    }

    // 销毁管理器
    net_mgr_destroy(mgr);
    return 0;
}