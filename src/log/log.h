#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h> 
#include <sys/types.h>

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

#define LOG_FILE_PATH       "/root/serial_server.log"
#define LOG_MAX_SIZE        (1024 * 1024 * 5)
#define LOG_LEVEL_DEFAULT   LOG_LEVEL_DEBUG
#define is_output_screen    1
static LogLevel g_log_level = LOG_LEVEL_DEFAULT;

void log_write(LogLevel level, const char *file, int line, const char *fmt, ...);

const char *log_level_to_str(LogLevel level);

#define LOG_DEBUG(fmt, ...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_write(LOG_LEVEL_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_write(LOG_LEVEL_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) log_write(LOG_LEVEL_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

int log_init(void);

void log_destroy(void);

#endif // !LOG_H