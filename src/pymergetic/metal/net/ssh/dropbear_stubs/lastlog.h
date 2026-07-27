#ifndef _METAL_DB_LASTLOG_H
#define _METAL_DB_LASTLOG_H
struct lastlog { long ll_time; char ll_line[32]; char ll_host[256]; };
#endif
