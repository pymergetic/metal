#ifndef _METAL_DB_SYSLOG_H
#define _METAL_DB_SYSLOG_H
#define LOG_PID 0x01
#define LOG_DAEMON 3<<3
#define LOG_AUTHPRIV 10<<3
#define LOG_INFO 6
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_ERR 3
#define LOG_DEBUG 7
void openlog(const char *ident, int option, int facility);
void closelog(void);
void syslog(int priority, const char *format, ...);
#endif
