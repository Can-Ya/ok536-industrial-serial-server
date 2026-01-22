#include "cli_mgr.h"

/*
static char* cli_command_generator(const char* text, int state);
static char** cli_command_completion(const char* text, int start, int end);
*/

//brief List of supported CLI commands (NULL-terminated)
static const char* cli_cmd_list[] = {
    "uart_status", "uart_set", "net_status", "log_level", "help", "exit", NULL
};  

/**
 * @brief Split input string into argument array
 * @param input: Input command string (will be modified by strtok)
 * @param argv: Output argument array
 * @param max_argc: Maximum number of arguments (including NULL terminator)
 * @return Number of parsed arguments on success, -1 on invalid input
 */
static int cli_split_args(char* input, char** argv, int max_argc)
{
    if (!input || !argv || max_argc <= 0) return -1;

    int argc = 0;
    char* token = strtok(input, " \t\n");
    while (token != NULL && argc < max_argc - 1) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL; // 结束符
    return argc;
}

/**
 * @brief Parse command type from argument array
 * @param argc: Number of arguments
 * @param argv: Argument array
 * @return Corresponding CliCmdType, CMD_UNKNOWN if command not recognized
 */
static CliCmdType cli_parse_cmd(int argc, char** argv)
{
    if (argc < 1) return CMD_UNKNOWN;

    if (strcmp(argv[0], "uart_status") == 0) return CMD_UART_STATUS;
    if (strcmp(argv[0], "uart_set") == 0) return CMD_UART_SET;
    if (strcmp(argv[0], "net_status") == 0) return CMD_NET_STATUS;
    if (strcmp(argv[0], "log_level") == 0) return CMD_LOG_LEVEL;
    if (strcmp(argv[0], "help") == 0) return CMD_HELP;
    if (strcmp(argv[0], "exit") == 0) return CMD_EXIT;

    return CMD_UNKNOWN;
}

/**
 * @brief Execute uart_status command
 * @param argc: Number of arguments
 * @param argv: Argument array (argv[1] = UART index)
 */
static void cli_exec_uart_status(int argc, char** argv)
{
    if (argc < 2) {
        LOG_WARN("Usage: uart_status <uart_idx> (0~%d)", MAX_UART_NUM - 1);
        return;
    }

    int uart_idx = atoi(argv[1]);
    if(uart_idx < 0 || uart_idx >= MAX_UART_NUM) {
        LOG_WARN("Usage: uart_idx must be 0~%d", MAX_UART_NUM - 1);
        return;
    }

    UartDev status;
    uart_mgr_get_status(g_uart_mgr, uart_idx, &status);
    printf("========= UART %d Status =========\n", uart_idx);
    printf("Dev Path:    %s\n", status.config.dev_path);
    printf("Enable:      %s\n", status.config.enable ? "YES" : "NO");
    printf("Modbus Enable: %s\n", status.config.modbus_enable ? "YES" : "NO");
    printf("Baudrate:    %d\n", status.config.baudrate);
    printf("Databit:     %d\n", status.config.databit);
    printf("Stopbit:     %d\n", status.config.stopbit);
    printf("Parity:      %c\n", status.config.parity);
    printf("Flow Ctrl:   %d\n", status.config.flow_ctrl);
    printf("RX Bytes:    %lu\n", status.rx_bytes);
    printf("TX Bytes:    %lu\n", status.tx_bytes);
    printf("Error Count: %u\n", status.err_count);
    printf("FD:          %d\n", status.fd);
    printf("==================================\n");
}

/**
 * @brief Execute uart_set command (to be implemented)
 * @param argc: Number of arguments
 * @param argv: Argument array
 */
static void cli_exec_uart_set(int argc, char** argv)
{
   if (argc < 3 || strcmp(argv[1], "-i") != 0) {
        LOG_WARN("Invalid usage!");
        LOG_WARN("Usage: uart_set -i <uart_idx> [-b <baud>] [-d <databit>] [-s <stopbit>]");
        LOG_WARN("                [-p <parity(N/E/O)>] [-e <enable(0/1)>] [-m <modbus_en(0/1)>]");
        LOG_WARN("Example: uart_set -i 0 -b 115200 -p N -e 1 -m 1");
        return;
    }

    int uart_idx = atoi(argv[2]);
    if (uart_idx < 0 || uart_idx >= MAX_UART_NUM) {
        LOG_WARN("Invalid uart_idx! Must be 0~%d", MAX_UART_NUM - 1);
        return;
    }

    UartDev uart_dev;
    uart_mgr_get_status(g_uart_mgr, uart_idx, &uart_dev);
    if (&uart_dev == 0) {
        LOG_ERROR("Failed to get UART %d status", uart_idx);
        return;
    }
    UartConfig new_config = uart_dev.config;

    for (int i = 3; i < argc; i += 2) {
        if (i + 1 >= argc) {
            LOG_WARN("Missing value for option: %s", argv[i]);
            return;
        }

        if (strcmp(argv[i], "-b") == 0) { 
            int baud = atoi(argv[i+1]);

            switch (baud) {
                case 9600: case 19200: case 38400: case 48000: case 57600:
                case 115200: case 230400: case 460800: case 921600:
                    new_config.baudrate = baud;
                    break;
                default:
                    LOG_WARN("Invalid baudrate! Supported: 9600/19200/38400/57600/115200/230400/460800/921600");
                    return;
            }
        }  else if (strcmp(argv[i], "-d") == 0) {
            int databit = atoi(argv[i+1]);
            if (databit < 5 || databit > 8) {
                LOG_WARN("Invalid databit! Must be 5~8");
                return;
            }
            new_config.databit = databit;
        } else if (strcmp(argv[i], "-s") == 0) { 
            int stopbit = atoi(argv[i+1]);
            if (stopbit != 1 && stopbit != 2) {
                LOG_WARN("Invalid stopbit! Must be 1 or 2");
                return;
            }
            new_config.stopbit = stopbit;
        } else if (strcmp(argv[i], "-p") == 0) {
            char parity = toupper(argv[i+1][0]);
            if (parity != 'N' && parity != 'E' && parity != 'O') {
                LOG_WARN("Invalid parity! Must be N (None)/E (Even)/O (Odd)");
                return;
            }
            new_config.parity = parity;
        } else if (strcmp(argv[i], "-e") == 0) {
            int enable = atoi(argv[i+1]);
            if (enable != 0 && enable != 1) {
                LOG_WARN("Invalid enable! Must be 0 (disable) or 1 (enable)");
                return;
            }
            new_config.enable = (enable == 1) ? 1 : 0;
        } else if (strcmp(argv[i], "-m") == 0) { 
            int modbus_en = atoi(argv[i+1]);
            if (modbus_en != 0 && modbus_en != 1) {
                LOG_WARN("Invalid modbus enable! Must be 0 (disable) or 1 (enable)");
                return;
            }
            new_config.modbus_enable = (modbus_en == 1) ? 1 : 0;
        } else { 
            LOG_WARN("Unknown option: %s", argv[i]);
            return;
        }
    }

    if (uart_mgr_set_config(g_uart_mgr, uart_idx, &new_config) != 0) {
        LOG_ERROR("Failed to set UART %d configuration", uart_idx);
        return;
    }

    LOG_INFO("UART %d configuration updated successfully!", uart_idx);
    printf("===== Updated UART %d Config =====\n", uart_idx);
    printf("Enable:      %s\n", new_config.enable ? "YES" : "NO");
    printf("Modbus Enable: %s\n", new_config.modbus_enable ? "YES" : "NO");
    printf("Baudrate:    %d\n", new_config.baudrate);
    printf("Databit:     %d\n", new_config.databit);
    printf("Stopbit:     %d\n", new_config.stopbit);
    printf("Parity:      %c\n", new_config.parity);
    printf("Flow Ctrl:   %d\n", new_config.flow_ctrl); // 保持原有值
    printf("==================================\n");
}

/**
 * @brief Execute log_level command
 * @param argc: Number of arguments
 * @param argv: Argument array (argv[1] = log level string)
 */
static void cli_exec_log_level(int argc, char** argv)
{
    if (argc < 2) {
        LOG_WARN("Usage: log_level <debug/info/warn/error/fatal>");
        return;
    }

    LogLevel level = LOG_LEVEL_DEFAULT;
    if (strcmp(argv[1], "debug") == 0) level = LOG_LEVEL_DEBUG;
    if (strcmp(argv[1], "info") == 0) level = LOG_LEVEL_INFO;
    if (strcmp(argv[1], "warn") == 0) level = LOG_LEVEL_WARN;
    if (strcmp(argv[1], "error") == 0) level = LOG_LEVEL_ERROR;
    if (strcmp(argv[1], "fatal") == 0) level = LOG_LEVEL_FATAL;
    else {
        LOG_WARN("Invalid log level %s", argv[1]);
        return;
    }

    g_log_level = level;
    LOG_INFO("Log level set to %s", log_level_to_str(level));
}

/**
 * @brief Execute net_status command
 * @param argc: Number of arguments
 * @param argv: Argument array
 */
static void cli_exec_net_status(int argc, char** argv)
{
    if (!g_net_mgr) {
        LOG_WARN("NetWodr manager not initialized\n");
        return;
    }

    printf("========= Network Status =========\n");
    printf("Mode: %s\n", 
           g_net_mgr->mode == NET_MODE_TCP_SERVER ? "TCP Server" :
           g_net_mgr->mode == NET_MODE_TCP_CLIENT ? "TCP Client" : "UDP");
    printf("Server FD: %d\n", g_net_mgr->server_fd);

    if (g_net_mgr->mode == NET_MODE_TCP_SERVER) {
        printf("Active TCP Clients:\n");
        for (int i = 0; i < MAX_CLIENT_NUM; i++) {
            if (g_net_mgr->clients[i].connected) {
                printf("  Client %d: %s:%d, RX Bytes: %lu, TX Bytes: %lu\n",
                       i,
                       inet_ntoa(g_net_mgr->clients[i].addr.sin_addr),
                       ntohs(g_net_mgr->clients[i].addr.sin_port),
                       g_net_mgr->clients[i].rx_bytes,
                       g_net_mgr->clients[i].tx_bytes);
            }
        }
    } else if (g_net_mgr->mode == NET_MODE_TCP_CLIENT) {
        printf("Client FD: %d\n", g_net_mgr->client_fd);
    }
    printf("==================================\n");
}

/**
 * @brief Execute help command (show usage of all supported commands)
 */
static void cli_exec_help(void)
{
    printf("===== Serial Server CLI Help =====\n");
    printf("uart_status <idx>    - Query UART <idx> status\n");
    printf("uart_set -i <idx> [-b <baud>] [-d <databit>] [-s <stopbit>] [-p <parity>]\n");
    printf("                     - Modify UART params (parity: N/E/O)\n");
    printf("log_level <level>    - Set log level (debug/info/warn/error/fatal)\n");
    printf("net_status           - Show network status\n");
    printf("help                 - Show this help\n");
    printf("exit                 - Exit CLI (server continues running)\n");
    printf("==================================\n");
}

/**
 * @brief Execute exit command (set g_running to 0 to exit CLI loop)
 */
static void cli_exec_exit(void)
{
    g_running = 0;
}

/**
 * @brief Dispatch and execute parsed CLI command
 * @param argc: Number of arguments
 * @param argv: Argument array
 */
static void cli_exec_cmd(int argc, char** argv)
{
    CliCmdType cmd = cli_parse_cmd(argc, argv);
    switch (cmd) {
        case CMD_UART_STATUS:
            cli_exec_uart_status(argc, argv);
            break;
        case CMD_UART_SET:
            cli_exec_uart_set(argc, argv);
            break;
        case CMD_NET_STATUS:
            cli_exec_net_status(argc, argv);
            break;
        case CMD_LOG_LEVEL:
            cli_exec_log_level(argc, argv);
            break;
        case CMD_HELP:
            cli_exec_help();
            break;
        case CMD_EXIT:
            cli_exec_exit();
            break;
        default:
            LOG_WARN("Unknown command: %s (type 'help' for usage)", argv[0]);
            break;
    }
}

/**
 * @brief CLI command completion callback for readline
 * @param text: Input text to complete
 * @param start: Start index of text in input line
 * @param end: End index of text in input line
 * @return Array of matched command strings (NULL-terminated), NULL on failure
 */
static char** cli_command_completion(const char* text, int start, int end)
{
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, cli_command_generator);
}

/**
 * @brief Generate matched CLI commands for auto-completion
 * @param text: Input text to match against command list
 * @param state: State flag for readline completion (0 = first call, non-0 = subsequent calls)
 * @return Pointer to matched command string (strdup'd), NULL if no more matches
 */
static char* cli_command_generator(const char* text, int state)
{
    static int list_idx, text_len;
    const char* cmd;

    if (!state) {
        list_idx = 0;
        text_len = strlen(text);
    }

    while ((cmd = cli_cmd_list[list_idx++]) != NULL) {
        if (strncmp(cmd, text, text_len) == 0) {
            return strdup(cmd);
        }
    }

    return NULL;
}

/**
 * @brief Initialize CLI manager
 *
 * Initializes readline library, sets up command completion callback,
 * and initializes command history (limit to 100 entries).
 * @return 0 on success, negative value on failure
 */
int cli_mgr_init(void)
{
    rl_initialize();
    rl_attempted_completion_function = cli_command_completion;

    using_history();
    stifle_history(100);

    return 0;
}

/**
 * @brief CLI main loop thread function
 *
 * Continuously prompts for user input, trims whitespace, skips empty input,
 * adds valid input to history, parses arguments, and executes commands.
 * Exits when g_running is 0 or readline returns NULL.
 * @param arg: Unused thread argument
 * @return NULL on exit
 */
void* cli_mgr_loop(void* arg)
{
    char* input;
    char* argv[32];
    int argc;

    LOG_INFO("CLI is ready (type 'help' for available commands)");
    while (g_running) {
        input = readline("serial_server > ");
        if (!input) break;

        char* trim_input = input;
        while (isspace((unsigned char)*trim_input)) trim_input++;
        if (*trim_input == '\0') {
            free(input);
            continue;
        }

        add_history(input);

        argc = cli_split_args(trim_input, argv, sizeof(argv)/sizeof(argv[0]));
        if (argc > 0) {
            cli_exec_cmd(argc, argv);
        }

        free(input);
    }

    return NULL;
}

/**
 * @brief Destroy CLI manager
 *
 * Clears command history, cleans up readline resources after signal,
 * and logs destruction completion.
 */
void cli_mgr_destroy(void)
{
    rl_clear_history();
    rl_cleanup_after_signal();
    LOG_INFO("CLI manager destroyed");
}
