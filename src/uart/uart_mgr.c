#include "uart_mgr.h"
#include "../log/log.h"

/**
 * Convert baudrate value to corresponding speed_t constant
 * @param baudrate: Input baudrate value (e.g. 9600, 115200)
 * @return Corresponding speed_t constant (Bxxx)
 */
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

/**
 * Set UART device attributes (baudrate, databit, parity, stopbit, flow control)
 * @param fd: UART device file descriptor
 * @param config: Pointer to UartConfig structure
 * @return 0 on success, -1 on failure
 */
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
        LOG_ERROR("Uart set attribute failed");
        return -1;
    }
    return 0;
}

/**
 * Open UART device and set attributes
 * @param config: Pointer to UartConfig structure
 * @return File descriptor on success, -1 on failure
 */
static int uart_open_device(UartConfig *config)
{
    int fd = open(config->dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        LOG_ERROR("Failed to open uart device %s",config->dev_path);
        return -1;
    }

    if (uart_set_attr(fd, config) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * Parse UART configuration from YAML file
 * @param config_path: Path to YAML config file
 * @param uart_configs: Output array of UartConfig
 * @param max_num: Max number of UART configs to parse
 * @return Number of parsed configs on success, -1 on failure
 */
static int parse_uart_config(const char* config_path, UartConfig* uart_configs, int max_num)
{
    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        LOG_ERROR("Failed to ope uart_config.yaml");
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
        LOG_ERROR("Failed yaml parser init");
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
                    if (uart_idx > max_num) {
                        LOG_WARN("Uart config num reach max: %d", max_num);
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
        LOG_ERROR("YAML parse error: %s\n", parser.problem);
        ret = -1;
    } else {
        ret = uart_idx;
    }

    yaml_parser_delete(&parser);
    yaml_event_delete(&event);
    fclose(fp);
    return ret;
}

/**
 * Handle UART read events (epoll callback)
 * @param mgr: Pointer to UartMgr instance
 * @param fd: UART device file descriptor
 */
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
        LOG_INFO("%s Read %ld bytes: %s\n", uart->config.dev_path, len, buf);
    } else if (len < 0 && errno != EAGAIN) {
        uart->err_count++;
        LOG_ERROR("UART read error");
    }
}

/**
 * Initialize UART manager
 * @param config_path: Path to YAML config file
 * @return Pointer to UartMgr instance on success, NULL on failure
 */
UartMgr* uart_mgr_init(const char* config_path)
{
    UartMgr* mgr = (UartMgr*)malloc(sizeof(UartMgr));
    if(!mgr) {
        LOG_ERROR("Malloc UartMgr failed");
        return NULL;
    }
    memset(mgr, 0, sizeof(UartMgr));

    UartConfig temp_configs[MAX_UART_NUM] = {0};
    int uart_count = parse_uart_config(config_path, temp_configs, MAX_UART_NUM);
    if(uart_count <= 0) {
        LOG_ERROR("Parse uart config failed, count: %d", uart_count);
        free(mgr);
        return NULL;
    }

    mgr->uart_count = 0;
    for (int i = 0; i < uart_count; i++) {
        int idx = temp_configs[i].idx;
        if (idx < 0 || idx >= MAX_UART_NUM) {
            LOG_WARN("Invalid uart idx: %d, skip\n", idx);
            continue;
        }
        mgr->uarts[idx].config = temp_configs[i];
        mgr->uart_count++;
    }

    mgr->epoll_fd = epoll_create1(0);
    if(mgr->epoll_fd < 0) {
        LOG_ERROR("Failed to creat epoll");
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
            LOG_ERROR("Init uart %d failed (path: %s)", idx, uart->config.dev_path);
            continue;
        }

        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = uart->fd;
        if(epoll_ctl(mgr->epoll_fd, EPOLL_CTL_ADD, uart->fd, &ev) < 0) {
            LOG_ERROR("Failed to epoll_ctl add uart fd");
            close(uart->fd);
            uart->fd = -1;
            continue;
        }

        LOG_INFO("UART %d init success: %s (baud:%d, data:%d, stop:%d, parity:%c)",
                idx, uart->config.dev_path, uart->config.baudrate,
                uart->config.databit, uart->config.stopbit, uart->config.parity);
    }

    return mgr;
}

/**
 * Destroy UART manager and release resources
 * @param mgr: Pointer to UartMgr instance
 */
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

/**
 * Write data to specified UART port
 * @param mgr: Pointer to UartMgr instance
 * @param uart_idx: UART index (0 ~ MAX_UART_NUM-1)
 * @param data: Data buffer to write
 * @param len: Length of data buffer
 * @return Number of bytes written on success, -1 on failure
 */
int uart_mgr_write(UartMgr* mgr, int uart_idx, const char* data, int len)
{
    if(!mgr || !data || len <= 0 || uart_idx < 0 || uart_idx >= MAX_UART_NUM) {
        LOG_ERROR("Invalid params (uart_idx: %d, len: %d)", uart_idx, len);
        return -1;
    }

    UartDev* uart = &mgr->uarts[uart_idx];
    if(uart->fd < 0 || !uart->config.enable) {
        LOG_ERROR("%s not enabled or fd invalid", uart->config.dev_path);
        return -1;
    }

    ssize_t ret = write(uart->fd, data, len);
    if(ret > 0) {
        uart->tx_bytes += ret;
        LOG_INFO("%s Write %ld bytes success (total tx: %lu)", 
                uart->config.dev_path, ret, uart->tx_bytes);
    } else {
        uart->err_count++;
        LOG_ERROR("UART write error");
    }
    return (int)ret;
}

/**
 * Get UART device status
 * @param mgr: Pointer to UartMgr instance
 * @param uart_idx: UART index (0 ~ MAX_UART_NUM-1)
 * @param status: Output UartDev structure
 */
void uart_mgr_get_status(UartMgr* mgr, int uart_idx, UartDev* status) {
    if (!mgr || !status || uart_idx < 0 || uart_idx >= MAX_UART_NUM) {
        memset(status, 0, sizeof(UartDev));
        return;
    }
    memcpy(status, &mgr->uarts[uart_idx], sizeof(UartDev));
}

/**
 * Get UART device by index
 * @param mgr: Pointer to UartMgr instance
 * @param uart_idx: UART index (0 ~ MAX_UART_NUM-1)
 * @return Pointer to UartDev instance on success, NULL on failure
 */
UartDev* uart_mgr_get_uart_by_idx(UartMgr* mgr, int uart_idx)
{
    if (!mgr || uart_idx < 0 || uart_idx >= MAX_UART_NUM) {
        return NULL;
    }
    return &mgr->uarts[uart_idx];
}

/**
 * Write Modbus RTU frame to specified UART port
 * @param mgr: Pointer to UartMgr instance
 * @param uart_idx: UART index (0 ~ MAX_UART_NUM-1)
 * @param rtu_frame: Pointer to ModbusRTUFrame structure
 * @return Number of bytes written on success, -1 on failure
 */
int modbus_rtu_frame_write(UartMgr* mgr, int uart_idx, const ModbusRTUFrame* rtu_frame)
{
    if (mgr == NULL || rtu_frame == NULL) {
        LOG_ERROR("Invalid input params");
        return -1;
    }
    if (rtu_frame->data_len > (MODBUS_MAX_FRAME_LEN - 3)) {
        LOG_ERROR("Modbus RTU frame data len exceed max");
        return -1;
    }
/*
    if (rtu_frame->crc == 0x0000 && rtu_frame->data_len > 0) {
        return -2;
    }
*/
    static uint8_t send_buf[MODBUS_MAX_FRAME_LEN] = {0};
    int total_send_len = 1 + 1 + rtu_frame->data_len + 2;
    int offset = 0;

    send_buf[offset++] = rtu_frame->slave_addr;
    send_buf[offset++] = rtu_frame->func_code;
    memcpy(&send_buf[offset], rtu_frame->data, rtu_frame->data_len);
    offset += rtu_frame->data_len;
    send_buf[offset++] = rtu_frame->crc & 0xFF;  
    send_buf[offset++] = (rtu_frame->crc >> 8) & 0xFF; 

    UartDev* uart = &mgr->uarts[uart_idx];
    if(uart->fd < 0 || !uart->config.enable) {
        LOG_ERROR("%s not enabled or fd invalid", uart->config.dev_path);
        return -1;
    }

    ssize_t ret = write(uart->fd, send_buf, total_send_len);
    if(ret > 0) {
        uart->tx_bytes += ret;
        LOG_INFO("%s Write %ld bytes success (total tx: %lu)", 
                uart->config.dev_path, ret, uart->tx_bytes);
    } else {
        uart->err_count++;
        LOG_ERROR("UART write error");
    }
    return (int)ret;
}

