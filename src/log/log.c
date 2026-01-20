#include "log.h"

// Static global variables (file scope only)
static FILE *g_log_fp = NULL;          /**< Log file handle */
static LogLevel g_log_level = LOG_LEVEL_DEFAULT;  /**< Current log filter level */
static unsigned long g_log_file_size = 0;  /**< Current log file size (bytes) */

/**
 * Convert log level to string
 * @param level: Log level to convert
 * @return Const string of log level name
 */
static const char *log_level_to_str(LogLevel level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        case LOG_LEVEL_INFO:    return "DEBUG";
        case LOG_LEVEL_WARN:    return "DEBUG";
        case LOG_LEVEL_ERROR:   return "DEBUG";
        case LOG_LEVEL_FATAL:   return "DEBUG";
        default:                return "UNKNOWN";
    }
}

/**
 * Check log file size and rotate if needed
 * Rename old log file with timestamp when max size reached
 * @return 0 on success, -1 on file open failure
 */
static int log_check_rotate(void)
{
    if (g_log_file_size >= LOG_MAX_SIZE) {
        if (g_log_fp) {
            fclose(g_log_fp);
            g_log_fp =NULL;
        }

        char old_log_path[256] = {0};
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        snprintf(old_log_path, sizeof(old_log_path),
                "%s.%04d%02d%02d_%02d%02d%02d", LOG_FILE_PATH,
                tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
                tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
        rename(LOG_FILE_PATH, old_log_path);

        g_log_file_size = 0;
    }

    if (!g_log_fp) {
        g_log_fp = fopen(LOG_FILE_PATH, "a+");
        if (!g_log_fp) {
            fprintf(stderr, "[ERROR] Faile to open log file: %s\n", LOG_FILE_PATH);
            return -1;
        }
        setvbuf(g_log_fp, NULL, _IOLBF, 0);
    }
    return 0;
}

/**
 * Initialize log system implementation
 * Create log directory if not exists, open log file
 * @return 0 on success, -1 on failure
 */
int log_init(void) 
{
    char log_dir[256] = {0};
    strncpy(log_dir, LOG_FILE_PATH, sizeof(log_dir));
    char *last_slash = strrchr(log_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (access(log_dir, F_OK) != 0) {
            if (mkdir(log_dir, 0755) != 0) {
                fprintf(stderr, "[ERROR] Failed to create log dir: %s\n", log_dir);
                return -1;
            }
        }
    }

    g_log_fp = fopen(LOG_FILE_PATH, "a+");
    if (!g_log_fp) {
        fprintf(stderr, "[ERROR] Failed to open log file: %s\n", LOG_FILE_PATH);
        return -1;
    }

    setvbuf(g_log_fp, NULL, _IOLBF, 0);

    fseek(g_log_fp, 0, SEEK_END);
    g_log_file_size = ftell(g_log_fp);

    LOG_INFO("Serial server log system init success. Log file: %s, max size: %dMB",
             LOG_FILE_PATH, LOG_MAX_SIZE / (1024 * 1024));

    return 0;
}

/**
 * Log write function implementation
 * Filter logs by level, format and write to file
 * @param level: Log severity level (LogLevel enum)
 * @param file: Source file name where log is generated
 * @param line: Line number in source file
 * @param fmt: Format string (same as printf)
 * @param ...: Variable arguments for format string
 */
void log_write(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    if (level < g_log_level) {
        return;
    }

    if (log_check_rotate() != 0) return;    

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char time_str[32] = {0};
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_now);

    char log_header[128] = {0};
    snprintf(log_header, sizeof(log_header), "[%s] [%s] [%s:%d]",
            time_str, log_level_to_str(level), file, line);

    fputs(log_header, g_log_fp);
    g_log_file_size += strlen(log_header);  

    va_list args, args_screen;
    va_start(args, fmt);
    
    g_log_file_size += vfprintf(g_log_fp, fmt, args);

    if(is_output_screen) {
        va_start(args_screen, fmt);
        fprintf(stderr, "[%s] ", log_level_to_str(level));
        vfprintf(stderr, fmt, args_screen);
        fprintf(stderr, "\n");
        va_end(args_screen);
    }

    va_end(args);
    

    fputs("\n", g_log_fp);
    g_log_file_size += 1;

    fflush(g_log_fp);
}

/**
 * Deinitialize log system implementation
 * Close log file and release resources
 */
void log_deinit(void)
{
    LOG_INFO("Serial server log system deinit.");
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

