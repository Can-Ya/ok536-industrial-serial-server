#ifndef CLI_MGR_H
#define CLI_MGR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "../uart/uart_mgr.h"
#include "../net/net_mgr.h"
#include "../log/log.h"


extern UartMgr* g_uart_mgr;  
extern NetMgr*  g_net_mgr; 
extern volatile int g_running;
extern LogLevel g_log_level;

typedef enum {
    CMD_UNKNOWN,
    CMD_UART_STATUS,    
    CMD_UART_SET,       
    CMD_NET_STATUS,        
    CMD_LOG_LEVEL,      
    CMD_HELP,           
    CMD_EXIT            
} CliCmdType;


int cli_mgr_init(void);

void* cli_mgr_loop(void* arg);

void cli_mgr_destroy(void);


#endif // !CLI_MGR_H
