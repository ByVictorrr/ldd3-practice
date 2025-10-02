// test_scull.c — user-space tester for scull
// Build: gcc -Wall -Wextra -O2 -o test_scull test_scull.c
// Usage examples:
//   ./test_scull --device /dev/scull0 --reset
//   ./test_scull -d /dev/scull0 --set-quantum 4096 --get-quantum
//   ./test_scull -d /dev/scull0 --write "hello" --seek 0 --read 5
//   ./test_scull --set-qset 1000 --get-qset
//
// Notes:
// - Requires scull_ioctl.h in the include path (same dir as this file is fine).
// - Some ioctls might require admin privileges if the driver checks capabilities.
// - Ensure /dev/scull0 exists (udev or manual mknod).
//
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "scull.h"

static const char *devpath = "/dev/scull0";

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void print_help(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -d, --device PATH          Device node (default: /dev/scull0)\n"
        "      --reset                SCULL_IOCRESET\n"
        "      --set-quantum N        SCULL_IOCSQUANTUM = N\n"
        "      --get-quantum          SCULL_IOCGQUANTUM\n"
        "      --set-qset N           SCULL_IOCSQSET = N\n"
        "      --get-qset             SCULL_IOCGQSET\n"
        "      --write STR            Write string to device\n"
        "      --read N               Read N bytes from device and print\n"
        "      --seek OFF[:WHENCE]    lseek to OFF (bytes); WHENCE=0|1|2 (default 0)\n"
        "      --append               Open with O_APPEND\n"
        "      --trunc                Open with O_TRUNC (when O_WRONLY/O_RDWR)\n"
        "      --help                 Show this help\n",
        prog);
}

static off_t parse_seek_arg(const char *s, int *whence_out) {
    char *colon = strchr(s, ':');
    int whence = SEEK_SET;
    char *endptr = NULL;
    off_t off = 0;

    if (colon) {
        off = strtoll(s, &endptr, 0);
        if (endptr != colon) {
            fprintf(stderr, "Invalid seek offset: %s\n", s);
            exit(EXIT_FAILURE);
        }
        int w = atoi(colon + 1);
        if (w == 0) whence = SEEK_SET;
        else if (w == 1) whence = SEEK_CUR;
        else if (w == 2) whence = SEEK_END;
        else {
            fprintf(stderr, "Invalid whence (must be 0,1,2): %s\n", colon + 1);
            exit(EXIT_FAILURE);
        }
    } else {
        off = strtoll(s, &endptr, 0);
        if (*endptr != '\0') {
            fprintf(stderr, "Invalid seek: %s\n", s);
            exit(EXIT_FAILURE);
        }
    }
    *whence_out = whence;
    return off;
}

int main(int argc, char **argv) {
    int want_reset = 0;
    int have_set_quantum = 0, have_get_quantum = 0;
    int have_set_qset = 0, have_get_qset = 0;
    long set_quantum = 0, set_qset = 0;
    const char *write_str = NULL;
    long read_n = -1;
    int do_seek = 0, seek_whence = SEEK_SET;
    off_t seek_off = 0;
    int oflags = O_RDWR;

    static struct option opts[] = {
        {"device",       required_argument, 0, 'd'},
        {"reset",        no_argument,       0,  1 },
        {"set-quantum",  required_argument, 0,  2 },
        {"get-quantum",  no_argument,       0,  3 },
        {"set-qset",     required_argument, 0,  4 },
        {"get-qset",     no_argument,       0,  5 },
        {"write",        required_argument, 0,  6 },
        {"read",         required_argument, 0,  7 },
        {"seek",         required_argument, 0,  8 },
        {"append",       no_argument,       0,  9 },
        {"trunc",        no_argument,       0, 10 },
        {"help",         no_argument,       0, 'h'},
        {0,0,0,0}
    };

    int c, idx;
    while ((c = getopt_long(argc, argv, "d:h", opts, &idx)) != -1) {
        switch (c) {
            case 'd': devpath = optarg; break;
            case 'h': print_help(argv[0]); return 0;
            case 1:   want_reset = 1; break;
            case 2:   have_set_quantum = 1; set_quantum = strtol(optarg, NULL, 0); break;
            case 3:   have_get_quantum = 1; break;
            case 4:   have_set_qset = 1; set_qset = strtol(optarg, NULL, 0); break;
            case 5:   have_get_qset = 1; break;
            case 6:   write_str = optarg; break;
            case 7:   read_n = strtol(optarg, NULL, 0); break;
            case 8:   do_seek = 1; seek_off = parse_seek_arg(optarg, &seek_whence); break;
            case 9:   oflags |= O_APPEND; break;
            case 10:  oflags |= O_TRUNC; break;
            default:  print_help(argv[0]); return 2;
        }
    }

    // Choose open mode: if only reading requested and no writes/ioctls that change state, allow O_RDONLY.
    int need_write = (write_str != NULL) || have_set_quantum || have_set_qset || want_reset || (oflags & O_TRUNC) || (oflags & O_APPEND);
    if (!need_write) oflags = O_RDONLY;

    int fd = open(devpath, oflags, 0666);
    if (fd < 0) die("open");

    int ret;

    if (want_reset) {
        ret = ioctl(fd, SCULL_IOCRESET);
        if (ret < 0) die("ioctl(SCULL_IOCRESET)");
        printf("Reset: OK\n");
    }

    if (have_set_quantum) {
        int v = (int)set_quantum;
        ret = ioctl(fd, SCULL_IOCSQUANTUM, &v);
        if (ret < 0) die("ioctl(SCULL_IOCSQUANTUM)");
        printf("Set quantum to %d: OK\n", v);
    }

    if (have_get_quantum) {
        int v = 0;
        ret = ioctl(fd, SCULL_IOCGQUANTUM, &v);
        if (ret < 0) die("ioctl(SCULL_IOCGQUANTUM)");
        printf("Current quantum: %d\n", v);
    }

    if (have_set_qset) {
        int v = (int)set_qset;
        ret = ioctl(fd, SCULL_IOCSQSET, &v);
        if (ret < 0) die("ioctl(SCULL_IOCSQSET)");
        printf("Set qset to %d: OK\n", v);
    }

    if (have_get_qset) {
        int v = 0;
        ret = ioctl(fd, SCULL_IOCGQSET, &v);
        if (ret < 0) die("ioctl(SCULL_IOCGQSET)");
        printf("Current qset: %d\n", v);
    }

    if (do_seek) {
        off_t pos = lseek(fd, seek_off, seek_whence);
        if (pos == (off_t)-1) die("lseek");
        printf("Seeked to %lld\n", (long long)pos);
    }

    if (write_str) {
        size_t len = strlen(write_str);
        ssize_t w = write(fd, write_str, len);
        if (w < 0) die("write");
        printf("Wrote %zd bytes\n", w);
    }

    if (read_n >= 0) {
        char *buf = malloc((size_t)read_n + 1);
        if (!buf) die("malloc");
        ssize_t r = read(fd, buf, (size_t)read_n);
        if (r < 0) die("read");
        buf[r] = '\0';
        printf("Read %zd bytes: ", r);
        // Print as both string and hex bytes for clarity
        fwrite(buf, 1, r, stdout);
        printf("\nHex:");
        for (ssize_t i = 0; i < r; ++i) printf(" %02x", (unsigned char)buf[i]);
        printf("\n");
        free(buf);
    }

    close(fd);
    return 0;
}
