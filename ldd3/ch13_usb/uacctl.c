// uacctl.c - tiny CLI for your UAC ioctls
// usage examples are below.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "uac_ioctl.h"

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--dev DEV] <cmd> [args]\n"
        "  cmds:\n"
        "    get-mute\n"
        "    set-mute <0|1>\n"
        "    get-vol\n"
        "    set-vol <s16 in 1/256 dB>\n"
        "    get-vol-min\n"
        "    get-vol-max\n"
        "Examples:\n"
        "  %s get-mute\n"
        "  %s set-mute 1\n"
        "  %s get-vol\n"
        "  %s set-vol -256   # -1.0 dB\n",
        argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    const char *dev = "/dev/uac0";
    int i = 1;

    if (argc < 2) { usage(argv[0]); return 2; }

    if (i+1 < argc && strcmp(argv[i], "--dev") == 0) {
        dev = argv[i+1];
        i += 2;
    }
    if (i >= argc) { usage(argv[0]); return 2; }

    const char *cmd = argv[i++];

    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open device"); return 1; }

    int v, rc = 0;

    if (strcmp(cmd, "get-mute") == 0) {
        if (ioctl(fd, UAC_IOC_GET_MUTE, &v) < 0) { perror("GET_MUTE"); rc = 1; }
        else printf("%d\n", v);

    } else if (strcmp(cmd, "set-mute") == 0) {
        if (i >= argc) { fprintf(stderr, "set-mute needs 0|1\n"); rc = 2; }
        else {
            v = atoi(argv[i]);
            if (v != 0 && v != 1) { fprintf(stderr, "mute must be 0 or 1\n"); rc = 2; }
            else if (ioctl(fd, UAC_IOC_SET_MUTE, &v) < 0) { perror("SET_MUTE"); rc = 1; }
        }

    } else if (strcmp(cmd, "get-vol") == 0) {
        if (ioctl(fd, UAC_IOC_GET_VOL, &v) < 0) { perror("GET_VOL"); rc = 1; }
        else printf("%d\n", v);

    } else if (strcmp(cmd, "set-vol") == 0) {
        if (i >= argc) { fprintf(stderr, "set-vol needs s16 value (1/256 dB)\n"); rc = 2; }
        else {
            v = atoi(argv[i]);
            if (ioctl(fd, UAC_IOC_SET_VOL, &v) < 0) { perror("SET_VOL"); rc = 1; }
        }

    } else if (strcmp(cmd, "get-vol-min") == 0) {
        if (ioctl(fd, UAC_IOC_GET_MIN_VOL, &v) < 0) { perror("GET_MIN_VOL"); rc = 1; }
        else printf("%d\n", v);

    } else if (strcmp(cmd, "get-vol-max") == 0) {
        if (ioctl(fd, UAC_IOC_GET_MAX_VOL, &v) < 0) { perror("GET_MAX_VOL"); rc = 1; }
        else printf("%d\n", v);

    } else {
        usage(argv[0]);
        rc = 2;
    }

    close(fd);
    return rc;
}
