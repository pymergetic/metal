/* Freestanding stub — minimal ctype for WAMR under EDK2. */
#ifndef _CTYPE_H
#define _CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

static inline int
isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'
           || c == '\v';
}

static inline int
isdigit(int c)
{
    return c >= '0' && c <= '9';
}

static inline int
isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline int
isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

static inline int
isprint(int c)
{
    return c >= 0x20 && c <= 0x7e;
}

static inline int
isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

static inline int
islower(int c)
{
    return c >= 'a' && c <= 'z';
}

static inline int
isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline int
tolower(int c)
{
    return isupper(c) ? (c + ('a' - 'A')) : c;
}

static inline int
toupper(int c)
{
    return islower(c) ? (c - ('a' - 'A')) : c;
}

#ifdef __cplusplus
}
#endif

#endif /* _CTYPE_H */
