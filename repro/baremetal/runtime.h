#ifndef RVV_HANDSON_RUNTIME_H
#define RVV_HANDSON_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

int putchar(int ch);
int puts(const char *s);
int printf(const char *fmt, ...);

void rvv_exit(int code) __attribute__((noreturn));

#endif
