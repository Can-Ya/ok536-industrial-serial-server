#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

// UART config default
#define DEFAULT_DEV1        "/dev/ttyAS7"
#define DEFAULT_DEV2        "/dev/ttyAS8"
#define DEFAULT_BAUDRATE    115200
#define DEFAULT_DATABIT     8
#define DEFAULT_STOPBIT     1
#define DEFAULT_PARITY      'N'
#define DEFAULT_FLOW_CTRL   0

// TCP config default
#define TCP_PORT            8888
#define BUF_SIZE            1024
#define MAX_CLIENT_NUM      5   
#define LISTEN_BACKLOG      5   

// Global share resource
int g_tcp_client_fd = -1;
int g_uart_fd[2] = {-1, -1};
pthread_mutex_t g_tcp_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function declare
static speed_t baudrate2bps(int baudrate);
static int uart_set_attr(int fd, speed_t speed, int databit, int stopbit, char parity, int flow_ctrl);
static int uart_open_device(const char *dev_path, speed_t speed, int databit, int stopbit, char parity, int flow_ctrl);
void *uart_read_thread(void *arg);
void *tcp_server_thread(void *arg);
int tcp_send_data(const char *uart_name, const char *data, int len);
void parse_tcp_data(char *buf, int len);

speed_t baudrate2bps(int baudrate)
{
    switch (baudrate)
    {
        case 50:        return B50;
        case 75:        return B75;
        case 110:       return B110;
        case 134:       return B134;
        case 150:       return B150;
        case 200:       return B200;
        case 300:       return B300;
        case 600:       return B600;
        case 1200:      return B1200;
        case 1800:      return B1800;
        case 2400:      return B2400;
        case 4800:      return B4800;
        case 9600:      return B9600;
        case 19200:     return B19200;
        case 38400:     return B38400;
        case 57600:     return B57600;
        case 115200:    return B115200;
        case 230400:    return B230400;
        case 460800:    return B460800;
        case 500000:    return B500000;
        case 576000:    return B576000;
        case 921600:    return B921600;
        case 1000000:   return B1000000;
        default:        return B115200;
    }
}

int uart_set_attr(int fd, speed_t speed, int databit, int stopbit, char parity, int flow_ctrl)
{
    struct termios uart_attr;
    memset(&uart_attr, 0, sizeof(uart_attr));

    uart_attr.c_cflag = speed | CLOCAL | CREAD;
    uart_attr.c_iflag = IGNPAR;
    uart_attr.c_oflag = 0;
    uart_attr.c_lflag = 0;
    uart_attr.c_cc[VMIN] = 1;
    uart_attr.c_cc[VTIME] = 0;

    uart_attr.c_cflag &= ~CSIZE;
    switch (databit)
    {
        case 5: uart_attr.c_cflag |= CS5; break;
        case 6: uart_attr.c_cflag |= CS6; break;
        case 7: uart_attr.c_cflag |= CS7; break;
        case 8: uart_attr.c_cflag |= CS8; break;
        default: uart_attr.c_cflag |= CS8; break;
    }

    switch (parity)
    {
        case 'O':
            uart_attr.c_cflag |= PARENB;
            uart_attr.c_cflag |= PARODD;
            uart_attr.c_iflag |= INPCK;
            break;
        case 'E':
            uart_attr.c_cflag |= PARENB;
            uart_attr.c_cflag &= ~PARODD;
            uart_attr.c_iflag |= INPCK;
            break;
        case 'N':
            uart_attr.c_cflag &= ~PARENB;
            break;
        default:
            uart_attr.c_cflag &= ~PARENB;
            break;
    }

    if (stopbit == 2)
        uart_attr.c_cflag |= CSTOPB;
    else 
        uart_attr.c_cflag &= ~CSTOPB;

    if(flow_ctrl)
        uart_attr.c_cflag |= CRTSCTS;
    else
        uart_attr.c_cflag &= ~CRTSCTS;

    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &uart_attr) != 0)
    {
        perror("uart set attribute failed");
        return -1;
    }
    return 0;
}

int uart_open_device(const char *dev_path, speed_t speed, int databit, int stopbit, char parity, int flow_ctrl)
{
    int fd = open(dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        perror("Failed to open uart device");
        fprintf(stderr, "Device path: %s\n", dev_path);
        return -1;
    }

    if (uart_set_attr(fd, speed, databit, stopbit, parity, flow_ctrl) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

void *uart_read_thread(void *arg)
{
    int uart_idx = *(int *)arg;
    char *uart_name = (uart_idx == 0) ? DEFAULT_DEV1 : DEFAULT_DEV2;
    int fd = g_uart_fd[uart_idx];
    char buf[BUF_SIZE] = {0};

    printf("UART read thread start: %s\n", uart_name);

    while (1)
    {
        ssize_t len = read(fd, buf, BUF_SIZE-1);
        if (len > 0)
        {
            buf[len] = '\0';
            char send_buf[BUF_SIZE + 32] = {0};
            snprintf(send_buf, sizeof(send_buf), "[%s] %s", uart_name, buf);
            tcp_send_data(uart_name, send_buf, strlen(send_buf));
            memset(buf, 0, sizeof(buf));
        }
        else if (len < 0 && errno != EAGAIN)
        {
            perror("UART read error");
            break;
        }
        usleep(1000);
    }
    pthread_exit(NULL);
}

int tcp_send_data(const char *uart_name, const char *data, int len)
{
    pthread_mutex_lock(&g_tcp_mutex);
    if (g_tcp_client_fd > 0)
    {
        ssize_t ret = send(g_tcp_client_fd, data, len, 0);
        if (ret < 0)
        {
            perror("TCP send error");
            g_tcp_client_fd = -1;
        }
        pthread_mutex_unlock(&g_tcp_mutex);
        return ret;
    }
    pthread_mutex_unlock(&g_tcp_mutex);
    return -1;
}

void parse_tcp_data(char *buf, int len)
{
    int uart_idx = 0;
    char data[BUF_SIZE] = {0};

    if (buf[0] == '[' && strstr(buf, "]") != NULL)
    {
        char *end = strstr(buf, "]");
        char uart_dev[32] = {0};
        strncpy(uart_dev, buf+1, end - buf -1);
        
        if (strcmp(uart_dev, "ttyAS8") == 0)
        {
            uart_idx = 1;
        }
        else if (strcmp(uart_dev, "ttyAS7") != 0)
        {
            printf("Invalid UART device: %s, use default ttyAS7\n", uart_dev);
            uart_idx = 0;
        }
        strncpy(data, end+1, len - (end - buf +1));
    }
    else
    {
        strncpy(data, buf, len);
    }

    if (g_uart_fd[uart_idx] > 0)
    {
        ssize_t ret = write(g_uart_fd[uart_idx], data, strlen(data));
        if (ret < 0)
        {
            perror("UART write error");
        }
        else
        {
            printf("Send to %s: %s\n", (uart_idx==0?DEFAULT_DEV1:DEFAULT_DEV2), data);
        }
    }
}

void *tcp_server_thread(void *arg)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("TCP socket create failed");
        pthread_exit(NULL);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(TCP_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("TCP bind failed");
        close(server_fd);
        pthread_exit(NULL);
    }

    if (listen(server_fd, LISTEN_BACKLOG) < 0)
    {
        perror("TCP listen failed");
        close(server_fd);
        pthread_exit(NULL);
    }

    printf("TCP server start, listen port: %d\n", TCP_PORT);

    while (1)
    {
        struct sockaddr_in client_addr;
        
        socklen_t client_len = sizeof(struct sockaddr_in);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd < 0)
        {
            perror("TCP accept failed");
            continue;
        }

        pthread_mutex_lock(&g_tcp_mutex);
        if (g_tcp_client_fd > 0)
        {
            close(g_tcp_client_fd);
            printf("Old client disconnected\n");
        }
        g_tcp_client_fd = client_fd;
        pthread_mutex_unlock(&g_tcp_mutex);

        printf("Client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        char buf[BUF_SIZE] = {0};
        while (1)
        {
            ssize_t len = recv(g_tcp_client_fd, buf, BUF_SIZE-1, 0);
            if (len <= 0)
            {
                perror("TCP recv error or client disconnect");
                pthread_mutex_lock(&g_tcp_mutex);
                g_tcp_client_fd = -1;
                pthread_mutex_unlock(&g_tcp_mutex);
                close(client_fd);
                memset(buf, 0, sizeof(buf));
                break;
            }
            buf[len] = '\0';
            printf("Recv from TCP: %s\n", buf);
            parse_tcp_data(buf, len);
            memset(buf, 0, sizeof(buf));
        }
    }
    close(server_fd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    speed_t speed = baudrate2bps(DEFAULT_BAUDRATE);
    g_uart_fd[0] = uart_open_device(DEFAULT_DEV1, speed, DEFAULT_DATABIT, DEFAULT_STOPBIT, DEFAULT_PARITY, DEFAULT_FLOW_CTRL);
    g_uart_fd[1] = uart_open_device(DEFAULT_DEV2, speed, DEFAULT_DATABIT, DEFAULT_STOPBIT, DEFAULT_PARITY, DEFAULT_FLOW_CTRL);
    
    if (g_uart_fd[0] < 0 || g_uart_fd[1] < 0)
    {
        fprintf(stderr, "UART init failed\n");
        if (g_uart_fd[0] > 0) close(g_uart_fd[0]);
        if (g_uart_fd[1] > 0) close(g_uart_fd[1]);
        return -1;
    }

    pthread_t uart_thread[2];
    int uart_idx[2] = {0, 1};
    for (int i = 0; i < 2; i++)
    {
        if (pthread_create(&uart_thread[i], NULL, uart_read_thread, &uart_idx[i]) != 0)
        {
            perror("Create uart thread failed");
            return -1;
        }
    }

    pthread_t tcp_thread;
    if (pthread_create(&tcp_thread, NULL, tcp_server_thread, NULL) != 0)
    {
        perror("Create TCP thread failed");
        return -1;
    }

    printf("UART<->TCP forward start successfully!\n");
    printf("TCP server: 192.168.1.232:%d\n", TCP_PORT);
    printf("UART1: %s, UART2: %s\n", DEFAULT_DEV1, DEFAULT_DEV2);
    printf("Usage:\n");
    printf("  Send to UART: [ttyAS7]data or [ttyAS8]data (default ttyAS7 if no prefix)\n");
    printf("  UART data will be forward to TCP with prefix [ttyASx]\n");

    pthread_join(tcp_thread, NULL);
    for (int i = 0; i < 2; i++)
    {
        pthread_join(uart_thread[i], NULL);
    }

    close(g_uart_fd[0]);
    close(g_uart_fd[1]);
    pthread_mutex_destroy(&g_tcp_mutex);
    return 0;
}