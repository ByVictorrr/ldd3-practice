// uacctl_fixed.c — userspace CLI for UAC ioctls with normalized dB/% I/O (C99-safe, no libm)
//
// Build:
//   gcc -O2 -Wall -Wextra -o uacctl uacctl_fixed.c
//
// Usage examples:
//   ./uacctl get-mute
//   ./uacctl set-mute 1
//   ./uacctl get-vol               # friendly output
//   ./uacctl get-vol --raw         # raw Q8.8
//   ./uacctl get-vol-min --raw
//   ./uacctl get-vol-max
//   ./uacctl get-vol-res
//   ./uacctl set-vol -10dB
//   ./uacctl set-vol --db -5
//   ./uacctl set-vol 37%
//   ./uacctl set-vol --raw -2560
//
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "uac_ioctl.h"

static inline int se16(int v) {        // sign-extend low 16 bits
    return (int)(int16_t)(v & 0xFFFF);
}
static inline int lo8(int v) {         // take low 8 bits
    return v & 0xFF;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--dev DEV] <cmd> [args] [--raw|--db|--pct]\n"
        "Commands:\n"
        "  get-mute                      Print 0/1\n"
        "  set-mute <0|1>\n"
        "  get-vol                       Print dB and percent (default). Use --raw for Q8.8\n"
        "  set-vol <VAL>                 VAL formats: raw Q8.8 (e.g. -2560), '-10dB', '37%%', or with --db/--pct\n"
        "  get-vol-min                   Device minimum volume\n"
        "  get-vol-max                   Device maximum volume\n"
        "  get-vol-res                   Device step size (Q8.8)\n"
        "Options:\n"
        "  --dev DEV                     Device node (default /dev/uac0)\n"
        "  --raw                         For getters: print raw Q8.8; For setters: treat input as raw Q8.8\n"
        "  --db                          For set-vol: treat input as dB\n"
        "  --pct                         For set-vol: treat input as percent of range (0-100)\n",
        argv0);
}

static int ioctl_get_int(int fd, unsigned long req, int *out) {
    if (ioctl(fd, req, out) < 0) return -1;
    return 0;
}

static int ioctl_set_int(int fd, unsigned long req, int val) {
    if (ioctl(fd, req, &val) < 0) return -1;
    return 0;
}

static double q88_to_db(int q88) { return ((double)q88) / 256.0; }

// No libm: manual rounding to nearest int for Q8.8
static int db_to_q88(double db) {
    double q = db * 256.0;
    q = (q >= 0.0) ? (q + 0.5) : (q - 0.5);
    if (q < -32768) q = -32768;
    if (q >  32767) q =  32767;
    return (int)q;
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Round 'v' to the nearest step starting at 'min', with step 'res' (all Q8.8 integers)
static int snap_to_res(int v, int min, int max, int res) {
    long off, step, snapped, out;
    if (res <= 0) res = 1;
    off = (long)v - (long)min;
    step = res;
    snapped = ((off + step/2) / step) * step;
    out = (long)min + snapped;
    if (out < min) out = min;
    if (out > max) out = max;
    return (int)out;
}

typedef enum {FMT_DEFAULT=0, FMT_RAW, FMT_DB, FMT_PCT} volfmt_t;

static void print_vol(int q88, int min_q88, int max_q88, volfmt_t fmt) {
    if (fmt == FMT_RAW) {
        printf("%d\n", q88);
        return;
    }
    {
        double db = q88_to_db(q88);
        double db_min = q88_to_db(min_q88);
        double db_max = q88_to_db(max_q88);
        double pct = (db_max != db_min) ? 100.0 * (db - db_min) / (db_max - db_min) : 0.0;
        if (pct < 0.0) pct = 0.0;
        if (pct > 100.0) pct = 100.0;
        printf("%.2f dB (%.1f%%)\n", db, pct);
    }
}

static int load_limits(int fd, int *minv, int *maxv, int *resv) {
    if (ioctl_get_int(fd, UAC_IOC_GET_MIN_VOL, minv) < 0) { perror("GET_MIN_VOL"); return -1; }
    if (ioctl_get_int(fd, UAC_IOC_GET_MAX_VOL, maxv) < 0) { perror("GET_MAX_VOL"); return -1; }
    if (ioctl_get_int(fd, UAC_IOC_GET_RES_VOL, resv) < 0) { perror("GET_RES_VOL"); return -1; }

    *minv = se16(*minv);
    *maxv = se16(*maxv);
    *resv = se16(*resv);

    if (*resv <= 0) *resv = 1;
    if (*minv > *maxv) { int t = *minv; *minv = *maxv; *maxv = t; }
    return 0;
}

static int parse_volume_arg(const char *s, volfmt_t hint, int min_q88, int max_q88, int res_q88, int *out_q88) {
    size_t n = strlen(s);
    char *end = NULL;

    // Suffix: dB
    if (n >= 2 && (s[n-2]=='d' || s[n-2]=='D') && (s[n-1]=='b' || s[n-1]=='B')) {
        double db = strtod(s, &end);
        if (end != s + (n-2)) return -EINVAL;
        {
            int q = db_to_q88(db);
            q = clampi(snap_to_res(q, min_q88, max_q88, res_q88), min_q88, max_q88);
            *out_q88 = q;
            return 0;
        }
    }
    // Suffix: percent
    if (n >= 1 && s[n-1] == '%') {
        double pct = strtod(s, &end);
        if (end != s + (n-1)) return -EINVAL;
        if (pct < 0.0) pct = 0.0;
        if (pct > 100.0) pct = 100.0;
        {
            double db_min = q88_to_db(min_q88);
            double db_max = q88_to_db(max_q88);
            double db = db_min + (pct/100.0) * (db_max - db_min);
            int q = db_to_q88(db);
            q = clampi(snap_to_res(q, min_q88, max_q88, res_q88), min_q88, max_q88);
            *out_q88 = q;
            return 0;
        }
    }

    // Flag-directed interpretation
    if (hint == FMT_DB) {
        double db = strtod(s, &end);
        if (end == s || *end) return -EINVAL;
        {
            int q = db_to_q88(db);
            q = clampi(snap_to_res(q, min_q88, max_q88, res_q88), min_q88, max_q88);
            *out_q88 = q;
            return 0;
        }
    } else if (hint == FMT_PCT) {
        double pct = strtod(s, &end);
        if (end == s || *end) return -EINVAL;
        if (pct < 0.0) pct = 0.0;
        if (pct > 100.0) pct = 100.0;
        {
            double db_min = q88_to_db(min_q88);
            double db_max = q88_to_db(max_q88);
            double db = db_min + (pct/100.0) * (db_max - db_min);
            int q = db_to_q88(db);
            q = clampi(snap_to_res(q, min_q88, max_q88, res_q88), min_q88, max_q88);
            *out_q88 = q;
            return 0;
        }
    } else {
        // Default: raw Q8.8
        long v;
        errno = 0;
        v = strtol(s, &end, 0);
        if (end == s || *end || errno == ERANGE) return -EINVAL;
        {
            int q = (int)v;
            q = clampi(snap_to_res(q, min_q88, max_q88, res_q88), min_q88, max_q88);
            *out_q88 = q;
            return 0;
        }
    }
}

int main(int argc, char **argv) {
    const char *dev = "/dev/uac0";
    int i = 1;
    int rc = 0;
    int fd;
    volfmt_t fmt = FMT_DEFAULT;
    int fmt_seen = 1;

    if (argc < 2) { usage(argv[0]); return 2; }

    // Optional --dev DEV
    if (i+1 < argc && strcmp(argv[i], "--dev") == 0) {
        dev = argv[i+1];
        i += 2;
    }
    if (i >= argc) { usage(argv[0]); return 2; }

    // Optional format flags
    while (i < argc) {
        if      (strcmp(argv[i], "--raw") == 0) fmt = FMT_RAW;
        else if (strcmp(argv[i], "--db")  == 0) fmt = FMT_DB;
        else if (strcmp(argv[i], "--pct") == 0) fmt = FMT_PCT;
        else { fmt_seen = 0; break; }
        i++;
    }

    if (i >= argc) { usage(argv[0]); return 2; }
    {
        const char *cmd = argv[i++];
        fd = open(dev, O_RDWR);
        if (fd < 0) { perror("open device"); return 1; }

        if (strcmp(cmd, "get-mute") == 0) {
            int v = 0;
            if (ioctl_get_int(fd, UAC_IOC_GET_MUTE, &v) < 0) { perror("GET_MUTE"); rc = 1; }
            else { v = lo8(v); printf("%d\n", v); }

        } else if (strcmp(cmd, "set-mute") == 0) {
            int v;
            if (i >= argc) { fprintf(stderr, "set-mute needs 0|1\n"); rc = 2; }
            else {
                v = atoi(argv[i]);
                v = (v != 0) ? 1 : 0;
                if (ioctl_set_int(fd, UAC_IOC_SET_MUTE, v) < 0) { perror("SET_MUTE"); rc = 1; }
            }

        } else if (strcmp(cmd, "get-vol") == 0) {
            int v=0, minv=0, maxv=0, resv=0;
            if (load_limits(fd, &minv, &maxv, &resv) < 0) { rc = 1; goto out; }
            if (ioctl_get_int(fd, UAC_IOC_GET_VOL, &v) < 0) { perror("GET_VOL"); rc = 1; }
            else { v = se16(v); print_vol(v, minv, maxv, fmt); }

        } else if (strcmp(cmd, "get-vol-min") == 0) {
            int v=0;
            if (ioctl_get_int(fd, UAC_IOC_GET_MIN_VOL, &v) < 0) { perror("GET_MIN_VOL"); rc = 1; }
            else if (fmt == FMT_RAW) printf("%d\n", se16(v));
            else printf("%.2f dB\n", q88_to_db(se16(v)));

        } else if (strcmp(cmd, "get-vol-max") == 0) {
            int v=0;
            if (ioctl_get_int(fd, UAC_IOC_GET_MAX_VOL, &v) < 0) { perror("GET_MAX_VOL"); rc = 1; }
            else if (fmt == FMT_RAW) printf("%d\n", se16(v));
            else printf("%.2f dB\n", q88_to_db(se16(v)));

        } else if (strcmp(cmd, "get-vol-res") == 0) {
            int v=0;
            if (ioctl_get_int(fd, UAC_IOC_GET_RES_VOL, &v) < 0) { perror("GET_RES_VOL"); rc = 1; }
            else if (fmt == FMT_RAW) printf("%d\n", se16(v));
            else printf("%.2f dB/step\n", q88_to_db(se16(v)));

        } else if (strcmp(cmd, "set-vol") == 0) {
            int minv=0, maxv=0, resv=0;
            if (i >= argc) { fprintf(stderr, "set-vol needs a value (raw Q8.8, XdB, or X%%)\n"); rc = 2; }
            else if (load_limits(fd, &minv, &maxv, &resv) < 0) { rc = 1; }
            else {
                volfmt_t parse_hint = fmt_seen ? fmt : FMT_DEFAULT;
                int q88 = 0;
                int perr = parse_volume_arg(argv[i], parse_hint, minv, maxv, resv, &q88);
                if (perr) {
                    fprintf(stderr, "set-vol: invalid value '%s'\n", argv[i]);
                    rc = 2;
                } else {
                    if (ioctl_set_int(fd, UAC_IOC_SET_VOL, q88) < 0) { perror("SET_VOL"); rc = 1; }
                    else {
                        int cur=0;
                        if (ioctl_get_int(fd, UAC_IOC_GET_VOL, &cur) == 0) {
                            cur = se16(cur);
                            fprintf(stdout, "Applied: ");
                            print_vol(cur, minv, maxv, FMT_DEFAULT);
                        }
                    }
                }
            }
        } else {
            usage(argv[0]);
            rc = 2;
        }
    }

out:
    close(fd);
    return rc;
}
