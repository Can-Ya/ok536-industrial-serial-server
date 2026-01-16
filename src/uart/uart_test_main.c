#include "uart_mgr.h"

int main() {
    // 1. 初始化串口管理器
    UartMgr* mgr = uart_mgr_init("/root/uart_config.yaml");
    if (!mgr) {
        fprintf(stderr, "UartMgr init failed\n");
        return -1;
    }

    // 打印所有串口启用状态（验证配置）
    printf("\n===== UART Enable Status =====\n");
    for (int idx = 0; idx < MAX_UART_NUM; idx++) {
        printf("UART %d: %s\n", idx, (mgr->uarts[idx].config.enable? "Enabled" : "Disabled"));
    }
    printf("==============================\n\n");

    // 2. 定义待测试的串口列表（3/5/7/8/11/12）
    //const int test_uart_list[] = {3, 5, 7, 8, 11, 12};
    const int test_uart_list[] = {3, 7, 11};
    const int test_uart_count = sizeof(test_uart_list) / sizeof(test_uart_list[0]);
    const int send_times = 1;  // 每个串口发送次数
    char send_buf[10] = "1597532846";
    int buf_len = sizeof(send_buf);

    // 3. 循环发送测试数据：每个串口发3次
    printf("===== Start Send Test Data =====\n");
    for (int t = 0; t < send_times; t++) {
        printf("\n--- Send Round %d ---\n", t+1);
        for (int i = 0; i < test_uart_count; i++) {
            int uart_idx = test_uart_list[i];
            int ret = uart_mgr_write(mgr, uart_idx, send_buf, buf_len);
            if (ret > 0) {
                printf("UART %d: Round %d send success\n", uart_idx, t+1);
            } else {
                fprintf(stderr, "UART %d: Round %d send failed (ret: %d)\n", uart_idx, t+1, ret);
            }
        }
    }
    printf("===== Send Test Data Finished =====\n\n");

    // 4. 进入epoll事件循环（阻塞，可接收串口数据/测试接收功能）
    printf("Start uart event loop (press Ctrl+C to exit)\n");
    uart_mgr_event_loop(mgr);

    // 5. 销毁管理器（事件循环退出后执行）
    uart_mgr_destroy(mgr);
    return 0;
}