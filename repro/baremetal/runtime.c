#include "runtime.h"

#include <stdarg.h>

enum {
    UART_BASE = 0x10000000u,
    UART_THR = UART_BASE + 0x00u,
    UART_LSR = UART_BASE + 0x05u,
    UART_LSR_THRE = 0x20u,
    FINISHER_BASE = 0x00100000u,
    FINISHER_PASS = 0x5555u,
    FINISHER_FAIL = 0x3333u,
};

static inline void mmio_write8(uintptr_t addr, uint8_t value)
{
    *(volatile uint8_t *)addr = value;
}

static inline uint8_t mmio_read8(uintptr_t addr)
{
    return *(volatile uint8_t *)addr;
}

static inline void mmio_write32(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

int putchar(int ch)
{
    if (ch == '\n') {
        putchar('\r');
    }

    while ((mmio_read8(UART_LSR) & UART_LSR_THRE) == 0) {
    }
    mmio_write8(UART_THR, (uint8_t)ch);
    return ch;
}

int puts(const char *s)
{
    while (*s != '\0') {
        putchar(*s++);
    }
    putchar('\n');
    return 0;
}

static void print_str(const char *s)
{
    if (s == 0) {
        s = "(null)";
    }
    while (*s != '\0') {
        putchar(*s++);
    }
}

static void print_unsigned(uint64_t value, unsigned base, int width, char pad)
{
    char buf[32];
    int pos = 0;

    do {
        unsigned digit = (unsigned)(value % base);
        buf[pos++] = (char)(digit < 10 ? ('0' + digit) : ('a' + digit - 10));
        value /= base;
    } while (value != 0);

    while (pos < width) {
        putchar(pad);
        width--;
    }

    while (pos > 0) {
        putchar(buf[--pos]);
    }
}

static void print_signed(int64_t value)
{
    uint64_t mag;

    if (value < 0) {
        putchar('-');
        mag = (uint64_t)(-(value + 1)) + 1u;
    } else {
        mag = (uint64_t)value;
    }
    print_unsigned(mag, 10, 0, ' ');
}

static void print_float(double value, int width, char pad)
{
    if (value < 0.0) {
        putchar('-');
        value = -value;
    }

    uint64_t whole = (uint64_t)value;
    double frac = value - (double)whole;
    uint32_t scaled = (uint32_t)(frac * 1000000.0 + 0.5);

    if (scaled >= 1000000u) {
        whole++;
        scaled -= 1000000u;
    }

    print_unsigned(whole, 10, width, pad);
    putchar('.');
    print_unsigned(scaled, 10, 6, '0');
}

int printf(const char *fmt, ...)
{
    int count = 0;
    va_list ap;
    va_start(ap, fmt);

    while (*fmt != '\0') {
        if (*fmt != '%') {
            putchar(*fmt++);
            count++;
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            putchar('%');
            fmt++;
            count++;
            continue;
        }

        char pad = ' ';
        int width = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        int long_count = 0;
        int size_t_arg = 0;
        if (*fmt == 'z') {
            size_t_arg = 1;
            fmt++;
        } else {
            while (*fmt == 'l') {
                long_count++;
                fmt++;
            }
        }

        switch (*fmt) {
        case 'c':
            putchar(va_arg(ap, int));
            break;
        case 's':
            print_str(va_arg(ap, const char *));
            break;
        case 'd':
        case 'i':
            if (size_t_arg) {
                print_signed((int64_t)va_arg(ap, size_t));
            } else if (long_count >= 2) {
                print_signed(va_arg(ap, long long));
            } else if (long_count == 1) {
                print_signed(va_arg(ap, long));
            } else {
                print_signed(va_arg(ap, int));
            }
            break;
        case 'u':
            if (size_t_arg) {
                print_unsigned(va_arg(ap, size_t), 10, width, pad);
            } else if (long_count >= 2) {
                print_unsigned(va_arg(ap, unsigned long long), 10, width, pad);
            } else if (long_count == 1) {
                print_unsigned(va_arg(ap, unsigned long), 10, width, pad);
            } else {
                print_unsigned(va_arg(ap, unsigned int), 10, width, pad);
            }
            break;
        case 'x':
            if (size_t_arg) {
                print_unsigned(va_arg(ap, size_t), 16, width, pad);
            } else if (long_count >= 2) {
                print_unsigned(va_arg(ap, unsigned long long), 16, width, pad);
            } else if (long_count == 1) {
                print_unsigned(va_arg(ap, unsigned long), 16, width, pad);
            } else {
                print_unsigned(va_arg(ap, unsigned int), 16, width, pad);
            }
            break;
        case 'f':
            print_float(va_arg(ap, double), width, pad);
            break;
        default:
            putchar('%');
            putchar(*fmt);
            break;
        }

        if (*fmt != '\0') {
            fmt++;
        }
    }

    va_end(ap);
    return count;
}

void rvv_exit(int code)
{
    uint32_t command = (code == 0) ? FINISHER_PASS : FINISHER_FAIL;
    uint32_t value = ((uint32_t)(code & 0xffff) << 16) | command;
    mmio_write32(FINISHER_BASE, value);

    for (;;) {
        __asm__ volatile("wfi");
    }
}
