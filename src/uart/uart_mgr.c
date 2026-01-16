#include "uart_mgr.h"

static speed_t baudrate2bps(int baudrate)
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

static int uart_set_attr(int fd, UartConfig* config)
{
    struct termios uart_attr;
    memset(&uart_attr, 0, sizeof(uart_attr));

    speed_t speed = baudrate2bps(config->baudrate);
    uart_attr.c_cflag = speed | CLOCAL | CREAD;
    uart_attr.c_iflag = IGNPAR;  
    uart_attr.c_oflag = 0;
    uart_attr.c_lflag = 0;
    uart_attr.c_cc[VMIN] = 1;    
    uart_attr.c_cc[VTIME] = 0;   

    uart_attr.c_cflag &= ~CSIZE;
    switch (config->databit)
    {
        case 5: uart_attr.c_cflag |= CS5; break;
        case 6: uart_attr.c_cflag |= CS6; break;
        case 7: uart_attr.c_cflag |= CS7; break;
        case 8: uart_attr.c_cflag |= CS8; break;
        default: uart_attr.c_cflag |= CS8; break;
    }

    switch (config->parity)
    {
        case 'O': 
            uart_attr.c_cflag |= PARENB | PARODD;
            uart_attr.c_iflag |= INPCK;
            break;
        case 'E': 
            uart_attr.c_cflag |= PARENB;
            uart_attr.c_cflag &= ~PARODD;
            uart_attr.c_iflag |= INPCK;
            break;
        case 'N': 
        default:
            uart_attr.c_cflag &= ~PARENB;
            break;
    }

    if (config->stopbit == 2)
        uart_attr.c_cflag |= CSTOPB;
    else 
        uart_attr.c_cflag &= ~CSTOPB;

    if(config->flow_ctrl)
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

static int uart_open_device(UartConfig *config)
{
    int fd = open(config->dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        perror("Failed to open uart device");
        fprintf(stderr, "Device path: %s\n", config->dev_path);
        return -1;
    }

    if (uart_set_attr(fd, config) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static int parse_uart_config(const char* config_path, UartConfig* uart_configs, int max_num)
{
    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        perror("[ERROR] open uart_config.yaml failed");
        return -1;
    }

    yaml_parser_t parser;
    yaml_event_t  event;
    memset(&parser, 0, sizeof(yaml_parser_t));
    memset(&event, 0, sizeof(yaml_event_t));

    int ret = -1;
    int uart_idx = 0;
    char current_key[64] = {0};
    int in_root_mapping = 0;   
    int in_uart_list_seq = 0; 
    int in_uart_item_map = 0;  

    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "[ERROR] yaml parser init failed\n");
        fclose(fp);
        return -1;
    }
    yaml_parser_set_input_file(&parser, fp);

    while (yaml_parser_parse(&parser, &event))
    {
        switch (event.type)
        {
            case YAML_STREAM_END_EVENT:
                goto parse_done;

            case YAML_MAPPING_START_EVENT:
                if (in_root_mapping == 0) {
                    in_root_mapping = 1;
                } else if (in_uart_list_seq == 1) {
                    in_uart_item_map = 1;
                    if (uart_idx < max_num) {
                        memset(&uart_configs[uart_idx], 0x00, sizeof(UartConfig));
                    }
                }
                break;

            case YAML_MAPPING_END_EVENT:
                if (in_uart_item_map == 1) {
                    in_uart_item_map = 0;
                    uart_idx++;
                    if (uart_idx >= max_num) {
                        fprintf(stderr, "[WARNING] Uart config num reach max: %d\n", max_num);
                    }
                } else if (in_root_mapping == 1) {
                    in_root_mapping = 0;
                }
                break;

            case YAML_SEQUENCE_START_EVENT:
                if (strcmp(current_key, "uart_list") == 0) {
                    in_uart_list_seq = 1;
                    memset(current_key, 0, sizeof(current_key));
                }
                break;

            case YAML_SEQUENCE_END_EVENT:
                if (in_uart_list_seq == 1) {
                    in_uart_list_seq = 0;
                }
                break;

            case YAML_SCALAR_EVENT:
            {
                char *val = (char *)event.data.scalar.value;
                if (!val || strlen(val) == 0) break;

                if (in_root_mapping == 1 && in_uart_list_seq == 0) {
                    strncpy(current_key, val, sizeof(current_key)-1);
                }
                else if (in_uart_list_seq == 1 && in_uart_item_map == 1 && strlen(current_key) == 0) {
                    strncpy(current_key, val, sizeof(current_key)-1);
                }
                else if (in_uart_list_seq == 1 && in_uart_item_map == 1 && strlen(current_key) > 0) {
                    if (uart_idx >= max_num) {
                        memset(current_key,0,sizeof(current_key));
                        break;
                    }
                    UartConfig *cfg = &uart_configs[uart_idx];

                    if (strcmp(current_key, "idx") == 0) {
                        cfg->idx = atoi(val);
                    }
                    else if (strcmp(current_key, "dev_path") == 0) {
                        strncpy(cfg->dev_path, val, sizeof(cfg->dev_path)-1);
                    }
                    else if (strcmp(current_key, "baudrate") == 0) {
                        cfg->baudrate = atoi(val);
                    }
                    else if (strcmp(current_key, "databit") == 0) {
                        cfg->databit = atoi(val);
                    }
                    else if (strcmp(current_key, "stopbit") == 0) {
                        cfg->stopbit = atoi(val);
                    }
                    else if (strcmp(current_key, "parity") == 0) {
                        cfg->parity = val[0];
                    }
                    else if (strcmp(current_key, "flow_ctrl") == 0) {
                        cfg->flow_ctrl = atoi(val);
                    }
                    else if (strcmp(current_key, "enable") == 0) {
                        cfg->enable = (strcmp(val, "true") == 0) ? 1 : 0;
                    }
                    else if (strcmp(current_key, "modbus_enable") == 0) {
                        cfg->modbus_enable = (strcmp(val, "true") == 0) ? 1 : 0;
                    }
                    memset(current_key, 0, sizeof(current_key));
                }
                break;
            }
            default:
                break;
        }
        yaml_event_delete(&event);
    }

parse_done:
    if (parser.error != YAML_NO_ERROR) {
        fprintf(stderr, "[ERROR] YAML parse error: %s\n", parser.problem);
        ret = -1;
    } else {
        ret = uart_idx;
    }

    yaml_parser_delete(&parser);
    yaml_event_delete(&event);
    fclose(fp);
    return ret;
}

static void uart_read_handler(UartMgr* mgr, int fd)
{
    if (!mgr) return;

    UartDev* uart = NULL;
    for(int i = 0; i < MAX_UART_NUM; i++) {
        if(mgr->uarts[i].fd == fd) {
            uart = &mgr->uarts[i];
            break;
        }
    }
    if(!uart) return;

    char buf[BUF_SIZE] = {0};
    ssize_t len = read(fd, buf, BUF_SIZE - 1);
    if(len > 0) {
        buf[len] = '\0';
        uart->rx_bytes += len;
        printf("[UART %s] Read %ld bytes: %s\n", uart->config.dev_path, len, buf);
    } else if (len < 0 && errno != EAGAIN) {
        uart->err_count++;
        perror("UART read error");
    }
}

UartMgr* uart_mgr_init(const char* config_path)
{
    UartMgr* mgr = (UartMgr*)malloc(sizeof(UartMgr));
    if(!mgr) {
        perror("Malloc UartMgr failed");
        return NULL;
    }
    memset(mgr, 0, sizeof(UartMgr));

    UartConfig temp_configs[MAX_UART_NUM] = {0};
    int uart_count = parse_uart_config(config_path, temp_configs, MAX_UART_NUM);
    if(uart_count <= 0) {
        fprintf(stderr, "Parse uart config failed, count: %d\n", uart_count);
        free(mgr);
        return NULL;
    }

    mgr->uart_count = 0;
    for (int i = 0; i < uart_count; i++) {
        int idx = temp_configs[i].idx;
        if (idx < 0 || idx >= MAX_UART_NUM) {
            fprintf(stderr, "Invalid uart idx: %d, skip\n", idx);
            continue;
        }
        mgr->uarts[idx].config = temp_configs[i];
        mgr->uart_count++;
    }

    mgr->epoll_fd = epoll_create1(0);
    if(mgr->epoll_fd < 0) {
        perror("epoll_creat failed");
        free(mgr);
        return NULL;
    }

    struct epoll_event ev;
    for( int idx = 0; idx < MAX_UART_NUM; idx++){
        UartDev* uart = &mgr->uarts[idx];
        if(!uart->config.enable) {
            continue;
        }

        uart->fd = uart_open_device(&uart->config);
        if(uart->fd < 0) {
            fprintf(stderr, "Init uart %d failed (path: %s)\n", idx, uart->config.dev_path);
            continue;
        }

        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = uart->fd;
        if(epoll_ctl(mgr->epoll_fd, EPOLL_CTL_ADD, uart->fd, &ev) < 0) {
            perror("epoll_ctl add uart fd failed");
            close(uart->fd);
            uart->fd = -1;
            continue;
        }

        printf("UART %d init success: %s (baud:%d, data:%d, stop:%d, parity:%c)\n",
               idx, uart->config.dev_path, uart->config.baudrate,
               uart->config.databit, uart->config.stopbit, uart->config.parity);
    }

    return mgr;
}

void uart_mgr_destroy(UartMgr* mgr)
{
    if (!mgr) return;
    
    for(int i = 0; i < MAX_UART_NUM; i++) {
        if(mgr->uarts[i].fd > 0) {
            close(mgr->uarts[i].fd);
            mgr->uarts[i].fd = -1;
        }
    }

    if(mgr->epoll_fd > 0) {
        close(mgr->epoll_fd);
    }

    free(mgr);
}

void uart_mgr_event_loop(UartMgr* mgr)
{
    if(!mgr) return;

    struct epoll_event events[EPOLL_MAX_EVENTS];
    while(1) {
        int nfds = epoll_wait(mgr->epoll_fd, events, EPOLL_MAX_EVENTS, 100);
        if (nfds < 0) {
            if(errno == EINTR) continue; 
            perror("epoll_wait failed");
            break;
        } else if (nfds == 0) {
            continue; 
        }

        for(int i = 0; i < nfds; i++) {
            if(events[i].events & EPOLLIN) {
                uart_read_handler(mgr, events[i].data.fd);
            } else if (events[i].events & EPOLLERR) {
                fprintf(stderr, "UART fd %d error\n", events[i].data.fd);
            }
        }
    }
}

int uart_mgr_write(UartMgr* mgr, int uart_idx, const char* data, int len)
{
    if(!mgr || !data || len <= 0 || uart_idx < 0 || uart_idx >= MAX_UART_NUM) {
        fprintf(stderr, "Invalid params (uart_idx: %d, len: %d)\n", uart_idx, len);
        return -1;
    }

    UartDev* uart = &mgr->uarts[uart_idx];
    if(uart->fd < 0 || !uart->config.enable) {
        fprintf(stderr, "UART %d not enabled or fd invalid (fd: %d)\n", uart_idx, uart->fd);
        return -1;
    }

    ssize_t ret = write(uart->fd, data, len);
    if(ret > 0) {
        uart->tx_bytes += ret;
        printf("[UART %d] Write %ld bytes success (total tx: %lu)\n", 
               uart_idx, ret, uart->tx_bytes);
    } else {
        uart->err_count++;
        perror("UART write error");
    }
    return (int)ret;
}

void uart_mgr_get_status(UartMgr* mgr, int uart_idx, UartDev* status) {
    if (!mgr || !status || uart_idx < 0 || uart_idx >= MAX_UART_NUM) {
        memset(status, 0, sizeof(UartDev));
        return;
    }
    memcpy(status, &mgr->uarts[uart_idx], sizeof(UartDev));
}