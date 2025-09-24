#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *errname(int e){
    return strerror(e); // short and sweet; strerrname_np is GNU-only
}

static void nap_ms(int ms){
    struct timespec ts = {.tv_sec = ms/1000, .tv_nsec = (ms%1000)*1000000L};
    nanosleep(&ts, NULL);
}

static void must(bool ok, const char *msg){
    if(!ok){ perror(msg); exit(1); }
}

static int open_report(const char *path, int flags){
    int fd = open(path, flags);
    if (fd < 0)
        fprintf(stderr, "open(%s, 0x%x) -> -1 (%s)\n", path, flags, errname(errno));
    else
        fprintf(stderr, "open(%s, 0x%x) -> %d OK\n", path, flags, fd);
    return fd;
}

static void close_report(int fd){
    if (fd >= 0) {
        int r = close(fd);
        fprintf(stderr, "close(%d) -> %d %s\n", fd, r, r ? errname(errno) : "OK");
    }
}


static void drop_to_uid(uid_t uid){
    // If we are root, only change the *effective* IDs so we can switch again later.
    if (getuid() == 0) {
        if (setegid(uid) < 0) { perror("setegid"); exit(1); }
        if (seteuid(uid) < 0) { perror("seteuid"); exit(1); }
    } else {
        // Not root: we can only permanently switch (may fail).
        if (setgid(uid) < 0) { perror("setgid"); exit(1); }
        if (setuid(uid) < 0) { perror("setuid"); exit(1); }
    }
    fprintf(stderr, "[proc %d] now running as ruid=%d euid=%d\n",
            getpid(), (int)getuid(), (int)geteuid());
}


static void test_scullsingle(const char *dev, uid_t uid1, uid_t uid2){
    fprintf(stderr, "\n=== TEST scullsingle on %s ===\n", dev);
    pid_t child = fork(); must(child>=0, "fork");
    if (child==0){
        drop_to_uid(uid1);
        int fd = open_report(dev, O_RDONLY);
        if (fd >= 0) pause(); // hold device until killed by parent
        exit(0);
    }
    nap_ms(200);
    drop_to_uid(uid2);
    int fd2 = open_report(dev, O_RDONLY);
    if (fd2 >= 0) {
        fprintf(stderr, "EXPECTED: second open should have failed with EBUSY. (Check module.)\n");
        close_report(fd2);
    } else {
        fprintf(stderr, "OK: second open failed as expected.\n");
    }
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
}

static void test_sculluid(const char *dev, uid_t uid1, uid_t uid2){
    fprintf(stderr, "\n=== TEST sculluid on %s ===\n", dev);
    pid_t child = fork(); must(child>=0, "fork");
    if (child==0){
        drop_to_uid(uid1);
        int fd = open_report(dev, O_RDONLY);
        if (fd >= 0) pause();
        exit(0);
    }
    nap_ms(200);

    // Same UID should succeed
    drop_to_uid(uid1);
    int same = open_report(dev, O_RDONLY);
    if (same >= 0) { fprintf(stderr, "OK: same-UID second open succeeded.\n"); close_report(same); }
    else { fprintf(stderr, "UNEXPECTED: same-UID open failed.\n"); }

    // Different UID should fail
    drop_to_uid(uid2);
    int diff = open_report(dev, O_RDONLY);
    if (diff >= 0) {
        fprintf(stderr, "UNEXPECTED: different-UID open succeeded (expected EBUSY).\n");
        close_report(diff);
    } else {
        fprintf(stderr, "OK: different-UID open failed as expected.\n");
    }
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
}
// Put these near the top of your file
static void switch_euid(uid_t uid) {
    // bounce to root first, then drop to target euid
    if (seteuid(0) < 0) { perror("seteuid(0)"); exit(1); }
    if (seteuid(uid) < 0) { perror("seteuid(uid)"); exit(1); }
    fprintf(stderr, "[proc %d] ruid=%d euid=%d\n", getpid(), getuid(), geteuid());
}

static int open_report_nb(const char *path, int flags) {
    int fd = open(path, flags | O_NONBLOCK);
    if (fd < 0) fprintf(stderr, "open(%s, 0x%x|O_NONBLOCK) -> -1 (%s)\n", path, flags, strerror(errno));
    else        fprintf(stderr, "open(%s, 0x%x|O_NONBLOCK) -> %d OK\n",  path, flags, fd);
    return fd;
}

static void test_scullwuid(const char *dev, uid_t writer_uid, uid_t other_uid){
    fprintf(stderr, "\n=== TEST scullwuid on %s ===\n", dev);

    // 1) First writer (owner) opens O_WRONLY (blocking is fine here)
    pid_t owner = fork(); must(owner>=0, "fork");
    if (owner == 0) {
        switch_euid(writer_uid);
        int fd = open_report(dev, O_WRONLY);          // blocking ok: first writer should pass
        if (fd >= 0) {
            const char *msg = "hello\n";
            ssize_t w = write(fd, msg, strlen(msg));
            fprintf(stderr, "write(owner euid=%d) -> %zd %s\n",
                    (int)geteuid(), w, (w<0)?strerror(errno):"OK");
            // hold the writer open so we can test contention
            pause();
            close(fd);
        }
        _exit(0);
    }
    nap_ms(200);

    // 2) Same-UID second writer (should be allowed per LDD3)
    switch_euid(writer_uid);
    int fd_same = open_report_nb(dev, O_WRONLY);
    if (fd_same >= 0) {
        fprintf(stderr, "OK: same-UID writer allowed while owner holds it.\n");
        close_report(fd_same);
    } else {
        if (errno == EAGAIN) fprintf(stderr, "UNEXPECTED: same-UID writer got EAGAIN.\n");
        else                 fprintf(stderr, "UNEXPECTED: same-UID writer failed: %s\n", strerror(errno));
    }

    // 3) Different-UID writer (should block or EAGAIN)
    switch_euid(other_uid);
    int fd_other = open_report_nb(dev, O_WRONLY);
    if (fd_other >= 0) {
        fprintf(stderr, "UNEXPECTED: other-UID writer succeeded (expected denial while owned).\n");
        close_report(fd_other);
    } else if (errno == EAGAIN || errno == EBUSY) {
        fprintf(stderr, "OK: other-UID writer denied nonblocking with %s while owner holds it.\n",
                (errno==EAGAIN)?"EAGAIN":"EBUSY");
    } else {
        fprintf(stderr, "UNEXPECTED: other-UID writer failed with %s (expected EAGAIN/EBUSY).\n",
                strerror(errno));
    }

    // 4) Clean up owner
    kill(owner, SIGTERM);
    waitpid(owner, NULL, 0);

    // 5) After owner closes, different-UID writer should succeed
    switch_euid(other_uid);
    int fd_after = open_report_nb(dev, O_WRONLY);
    if (fd_after >= 0) {
        fprintf(stderr, "OK: other-UID writer succeeded after owner closed.\n");
        close_report(fd_after);
    } else {
        fprintf(stderr, "UNEXPECTED: other-UID writer still denied after close: %s\n", strerror(errno));
    }
}

static void test_scullpriv(const char *dev){
    fprintf(stderr, "\n=== TEST scullpriv on %s ===\n", dev);

    // Non-root attempt
    if (getuid() == 0) {
        // Spawn an unprivileged child
        pid_t p = fork(); must(p>=0, "fork");
        if (p==0){
            // Drop to nobody (often 65534) if present
            drop_to_uid(65534);
            int fd = open_report(dev, O_RDONLY);
            if (fd >= 0) close_report(fd);
            exit(0);
        }
        waitpid(p, NULL, 0);
    } else {
        int fd = open_report(dev, O_RDONLY);
        if (fd >= 0) {
            fprintf(stderr, "UNEXPECTED: non-root open succeeded, expected EPERM.\n");
            close_report(fd);
        } else {
            fprintf(stderr, "OK: non-root denied as expected.\n");
        }
    }

    // Root attempt
    if (getuid() != 0) {
        fprintf(stderr, "Run as root to also verify root access.\n");
    } else {
        int fd = open_report(dev, O_RDONLY);
        if (fd >= 0) { fprintf(stderr, "OK: root open succeeded.\n"); close_report(fd); }
        else { fprintf(stderr, "UNEXPECTED: root open failed: %s\n", errname(errno)); }
    }
}

static void usage(const char *argv0){
    fprintf(stderr,
        "Usage:\n"
        "  %s single  <dev> [uid1 uid2]\n"
        "  %s uid     <dev> [uid1 uid2]\n"
        "  %s wuid    <dev> [writer_uid other_uid]\n"
        "  %s priv    <dev>\n"
        "\n"
        "Notes:\n"
        "- Run as root if you want the tool to switch UIDs internally.\n"
        "- If UIDs omitted: uid1=geteuid(), uid2=uid1+1 (best-effort).\n",
        argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv){
    if (argc < 3) { usage(argv[0]); return 2; }

    const char *mode = argv[1];
    const char *dev  = argv[2];

    uid_t uid1 = (argc >= 4) ? (uid_t)strtoul(argv[3], NULL, 10) : geteuid();
    uid_t uid2 = (argc >= 5) ? (uid_t)strtoul(argv[4], NULL, 10) : (uid1 + 1);

    if (!strcmp(mode, "single")) {
        test_scullsingle(dev, uid1, uid2);
    } else if (!strcmp(mode, "uid")) {
        test_sculluid(dev, uid1, uid2);
    } else if (!strcmp(mode, "wuid")) {
        test_scullwuid(dev, uid1, uid2);
    } else if (!strcmp(mode, "priv")) {
        test_scullpriv(dev);
    } else {
        usage(argv[0]);
        return 2;
    }
    return 0;
}
