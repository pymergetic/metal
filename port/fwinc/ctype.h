#ifndef PM_METAL_FW_CTYPE_H
#define PM_METAL_FW_CTYPE_H

static inline __attribute__((unused)) int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static inline __attribute__((unused)) int isdigit(int c) {
    return c >= '0' && c <= '9';
}
static inline __attribute__((unused)) int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static inline __attribute__((unused)) int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}
static inline __attribute__((unused)) int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static inline __attribute__((unused)) int toupper(int c) {
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}
static inline __attribute__((unused)) int tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

#endif
