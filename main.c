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

#define DEFAULT_DEV1        "/dev/ttyAS7"
#define DEFAULT_DEV2        "/dev/ttyAS8"
#define DEFAULT_MODE        'write'
#define DEFAULT_BAUDRATE    115200
#define DEFAULT_SEND_CNT    100
#define DEFAULT_DATA_LEN    10
#define DEFAULT_DATABIT     8
#define DEFAULT_STOPBIT     1
#define DEFAULT_PARITY      'N'
#define DEFAULT_FLOW_CTRL   0

static void get_rand_str(char buf[], size_t length);
static void print_usage(const char *prog_name);
static speed_t baudrate2bps(int baudrate);
static int get_transfer_delay(int databit, int stopbit, int baudrate, int data_len);
static int uart_set_attr(int fd, speed_t speed, int databit, int stopbit, char parity, int flow_ctrl);
static int uart_open_device(const char *dev_path, speed_t speed, int databit, int stopbit, char parity, int flow_ctrl);



void get_rand_str(char buf[], size_t length)
{
    char *charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    size_t charset_len = strlen(charset);
    struct timespec seed_time = { 0, 0 };

    clock_gettime(CLOCK_REALTIME, &seed_time);
    srand((unsigned int)(seed_time.tv_sec + seed_time.tv_nsec));
    
    for(size_t i = 0; i < length; i++) {
        buf[i] = charset[(rand() % charset_len)];
    }
    buf[length] = '\0';
}

void print_usage(const char *prog_name)
{
    printf("Usage:%s [Options]\n",prog_name);
    printf("Options:\n");
    printf("  -1 <dev>    Set UART device 1 path, default: %s\n", DEFAULT_DEV1);
    printf("  -2 <dev>    Set UART device 2 path, default: %s\n", DEFAULT_DEV2);
    printf("  -b  <rate>   Set baudrate, support:50-1000000, default: %d\n", DEFAULT_BAUDRATE);
    printf("  -n  <count>  Set total send round count, default: %d\n", DEFAULT_SEND_CNT);
    printf("  -l  <len>    Set single send data length(byte), default: %d\n", DEFAULT_DATA_LEN);
    printf("  -D <bit>    Set data bit (5/6/7/8), default: %d\n", DEFAULT_DATABIT);
    printf("  -S <bit>    Set stop bit (1/2), default: %d\n", DEFAULT_STOPBIT);
    printf("  -p  <parity> Set parity (N:None, O:Odd, E:Even), default: %c\n", DEFAULT_PARITY);
    printf("  -f  <ctrl>   Set hardware flow ctrl (0:off,1:on), default: %d\n", DEFAULT_FLOW_CTRL);
    printf("  -h           Show this help message\n");
}

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

int get_transfer_delay(int databit, int stopbit, int baudrate, int data_len)
{
    float delay_us = (1.0f / baudrate) * 1000000 * (1 + databit + stopbit) * data_len;
    return (int)delay_us + 1000;
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

int main(int argc, char *argv[])
{
    char *dev1 = DEFAULT_DEV1;
    char *dev2 = DEFAULT_DEV2;
    int baudrate = DEFAULT_BAUDRATE;
    int send_count = DEFAULT_SEND_CNT;
    int data_length = DEFAULT_DATA_LEN;
    int databit = DEFAULT_DATABIT;
    int stopbit = DEFAULT_STOPBIT;
    char parity = DEFAULT_PARITY;
    int flow_ctrl = DEFAULT_FLOW_CTRL;

    int opt = 0;
    const char *opt_string = "1:2:b:n:l:D:S:p:f:h";
    while ((opt = getopt(argc, argv, opt_string)) != -1)
    {
        switch (opt)
        {
            case '1': dev1 = optarg; break;          
            case '2': dev2 = optarg; break;          
            case 'b':  baudrate = atoi(optarg); break;
            case 'n':  send_count = atoi(optarg); break;
            case 'l':  data_length = atoi(optarg); break;
            case 'D': databit = atoi(optarg); break; 
            case 'S': stopbit = atoi(optarg); break; 
            case 'p':  parity = optarg[0]; break;
            case 'f':  flow_ctrl = atoi(optarg); break;
            case 'h':  print_usage(argv[0]); return 0;
            default:   print_usage(argv[0]); return -1;
        }
    }

    if(send_count <=0 || data_length <=0 || databit<5||databit>8 || (stopbit!=1&&stopbit!=2) || (parity!='N'&&parity!='O'&&parity!='E'))
    {
        fprintf(stderr, "Invalid parameter value,please check!\n");
        print_usage(argv[0]);
        return -1;
    }

    speed_t speed = baudrate2bps(baudrate);
    int fd_uart1 = uart_open_device(dev1, speed, databit, stopbit, parity, flow_ctrl);
    int fd_uart2 = uart_open_device(dev2, speed, databit, stopbit, parity, flow_ctrl);

    if (fd_uart1 < 0 || fd_uart2 < 0)
    {
        fprintf(stderr, "UART device init failed!\n");
        if(fd_uart1 > 0) close(fd_uart1);
        if(fd_uart2 > 0) close(fd_uart2);
        return -1;
    }

    char *tx_buffer = (char *)malloc(data_length + 1);
    char *rx_buffer = (char *)malloc(data_length + 1);
    if(tx_buffer == NULL || rx_buffer == NULL)
    {
        perror("malloc buffer failed");
        close(fd_uart1);
        close(fd_uart2);
        return -1;
    }

    printf("=============================================\n");
    printf("UART Loop Test Start, Config Info:\n");
    printf("UART1: %s, UART2: %s\n", dev1, dev2);
    printf("Baudrate: %d, Data bit: %d, Stop bit: %d\n", baudrate, databit, stopbit);
    printf("Parity: %c, Flow Ctrl: %s\n", parity, flow_ctrl?"ON":"OFF");
    printf("Data Length: %d byte, Total Round: %d\n", data_length, send_count);
    printf("=============================================\n\n");

    for (int round = 1; round <= send_count; round++)
    {
        printf("=== Round %d / %d ===\n", round, send_count);
        memset(tx_buffer, 0, data_length + 1);
        memset(rx_buffer, 0, data_length + 1);

        get_rand_str(tx_buffer, data_length);
        printf("[%s] TX: %s\n", dev1, tx_buffer);

        size_t write_len = 0;
        while(write_len < data_length)
        {
            ssize_t ret = write(fd_uart1, tx_buffer + write_len, data_length - write_len);
            if (ret < 0)
            {
                perror("uart1 write error");
                break;
            }
            write_len += ret;
        }
        usleep(get_transfer_delay(databit, stopbit, baudrate, data_length));
        
        size_t read_len = 0;
        while(read_len < data_length)
        {
            ssize_t ret = read(fd_uart2, rx_buffer + read_len, data_length - read_len);
            if(ret < 0)
            {
                perror("uart2 read error");
                break;
            }
            read_len += ret;
        }
        printf("[%s] RX: %s\n", dev2, rx_buffer);

        sleep(1);

        memset(tx_buffer, 0, data_length + 1);
        memset(rx_buffer, 0, data_length + 1);
        get_rand_str(tx_buffer, data_length);
        printf("[%s] TX: %s\n", dev2, tx_buffer);

        write_len = 0;
        while(write_len < data_length)
        {
            ssize_t ret = write(fd_uart2, tx_buffer + write_len, data_length - write_len);
            if(ret < 0)
            {
                perror("uart2 write error");
                break;
            }
            write_len += ret;
        }
        usleep(get_transfer_delay(databit, stopbit, baudrate, data_length));

        read_len = 0;
        while(read_len < data_length)
        {
            ssize_t ret = read(fd_uart1, rx_buffer + read_len, data_length - read_len);
            if(ret < 0)
            {
                perror("uart1 read error");
                break;
            }
            read_len += ret;
        }
        printf("[%s] RX: %s\n", dev1, rx_buffer);
        printf("\n");
        sleep(1);
    }
    
    free(tx_buffer);
    free(rx_buffer);
    close(fd_uart1);
    close(fd_uart2);

    printf("=============================================\n");
    printf("UART Loop Test Completed Successfully!\n");
    printf("=============================================\n");
    return 0;

}



