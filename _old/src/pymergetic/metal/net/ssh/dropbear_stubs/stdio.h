#ifndef _METAL_DB_STDIO_H
#define _METAL_DB_STDIO_H
#ifndef _STDIO_H
#define _STDIO_H
#endif
#include <stddef.h>
#include <stdarg.h>
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#ifndef EOF
#define EOF (-1)
#endif
typedef struct {
  int _fd;
} FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
int          printf(const char *fmt, ...);
int          fprintf(FILE *f, const char *fmt, ...);
int          sprintf(char *s, const char *fmt, ...);
int          snprintf(char *s, size_t n, const char *fmt, ...);
int          vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int          vprintf(const char *fmt, va_list ap);
int          sscanf(const char *str, const char *fmt, ...);
FILE        *fopen(const char *path, const char *mode);
int          fclose(FILE *f);
size_t       fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t       fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int          fputc(int c, FILE *f);
int          fgetc(FILE *f);
int          getc(FILE *f);
int          putchar(int c);
char        *fgets(char *s, int size, FILE *f);
int          fseek(FILE *f, long offset, int whence);
long         ftell(FILE *f);
int          fflush(FILE *f);
#endif
