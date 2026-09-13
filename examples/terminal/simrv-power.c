/* SPDX-License-Identifier: MIT
 * Minimal guest-side control utility for SimRV's SiFive test finisher.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SIMRV_POWER_ADDRESS UINT64_C(0x00100000)
#define SIMRV_POWEROFF UINT32_C(0x5555)
#define SIMRV_CRASH UINT32_C(0x3333)
#define SIMRV_REBOOT UINT32_C(0x7777)
#define SIMRV_EXIT UINT32_C(0x8888)

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage: %s poweroff|halt|reboot|exit|crash [status]\n"
            "Write a lifecycle request to the SimRV power MMIO device via /dev/mem.\n",
            program);
}

static int parse_status(const char *text, uint32_t *status) {
    char *end = NULL;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > UINT16_MAX) {
        fprintf(stderr, "simrv-power: invalid status '%s' (expected 0..65535)\n", text);
        return -1;
    }
    *status = (uint32_t)value;
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout, argv[0]);
        return 0;
    }
    if (argc < 2 || argc > 3) {
        usage(stderr, argv[0]);
        return 2;
    }

    uint32_t command = 0;
    const int accepts_status = strcmp(argv[1], "poweroff") == 0 ||
                               strcmp(argv[1], "halt") == 0 || strcmp(argv[1], "exit") == 0 ||
                               strcmp(argv[1], "crash") == 0;
    if (strcmp(argv[1], "reboot") == 0) {
        command = SIMRV_REBOOT;
    } else if (strcmp(argv[1], "crash") == 0) {
        command = SIMRV_CRASH;
    } else if (strcmp(argv[1], "exit") == 0) {
        command = SIMRV_EXIT;
    } else if (accepts_status) {
        command = SIMRV_POWEROFF;
    } else {
        fprintf(stderr, "simrv-power: unknown action '%s'\n", argv[1]);
        usage(stderr, argv[0]);
        return 2;
    }

    uint32_t status = 0;
    if (argc == 3 && (!accepts_status || parse_status(argv[2], &status) != 0)) {
        if (!accepts_status) {
            fprintf(stderr, "simrv-power: reboot does not accept a status\n");
        }
        return 2;
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        perror("simrv-power: sysconf(_SC_PAGESIZE)");
        return 1;
    }
    const uint64_t page_base_value =
        SIMRV_POWER_ADDRESS - (SIMRV_POWER_ADDRESS % (uint64_t)page_size);
    const off_t page_base = (off_t)page_base_value;
    const size_t register_offset = (size_t)(SIMRV_POWER_ADDRESS - page_base_value);

    const int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("simrv-power: open /dev/mem");
        return 1;
    }
    void *mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page_base);
    if (mapping == MAP_FAILED) {
        perror("simrv-power: mmap power device");
        close(fd);
        return 1;
    }

    volatile uint32_t *const power_register =
        (volatile uint32_t *)((unsigned char *)mapping + register_offset);
    *power_register = (status << 16U) | command;
    __sync_synchronize();

    /* A recognized request stops or reboots the simulated machine immediately. */
    fprintf(stderr, "simrv-power: lifecycle request returned without stopping the guest\n");
    munmap(mapping, (size_t)page_size);
    close(fd);
    return 1;
}
