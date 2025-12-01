#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

static const char *dev_path = "/dev/scullv0";

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void test_basic_mmap(int fd)
{
    const char *msg = "hello from write() into scullv\n";
    size_t len = strlen(msg);
    ssize_t ret;
    off_t pos;

    printf("== basic mmap test ==\n");

    /* Start from offset 0 */
    pos = lseek(fd, 0, SEEK_SET);
    if (pos < 0)
        die("lseek");

    /* Write some data into the device */
    ret = write(fd, msg, len);
    if (ret < 0)
        die("write");
    if ((size_t)ret != len) {
        fprintf(stderr, "short write: %zd\n", ret);
        exit(EXIT_FAILURE);
    }

    printf("wrote %zu bytes via write(): \"%s\"\n", len, msg);

    /* mmap exactly len bytes starting at offset 0 */
    void *map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        die("mmap");

    printf("mmap returned %p\n", map);
    printf("data seen via mmap: \"%.*s\"\n", (int)len, (char *)map);

    /* Check that the data in the mapping matches what we wrote */
    if (memcmp(map, msg, len) != 0) {
        fprintf(stderr, "ERROR: mmap contents do not match written data\n");
        munmap(map, len);
        exit(EXIT_FAILURE);
    } else {
        printf("mmap contents match written data ✅\n");
    }

    /* Now modify the mapped data and then read it back with read() */
    const char *patch = "MMAP!";
    size_t patch_len = strlen(patch);

    printf("patching mmap contents at offset 0 with \"%s\"\n", patch);
    memcpy(map, patch, patch_len);

    /* Make sure changes are visible – MAP_SHARED means same backing store */
    pos = lseek(fd, 0, SEEK_SET);
    if (pos < 0)
        die("lseek");

    char buf[256] = {0};
    ret = read(fd, buf, sizeof(buf));
    if (ret < 0)
        die("read");

    printf("read() after mmap modification: \"%.*s\"\n", (int)ret, buf);

    if (strncmp(buf, patch, patch_len) != 0) {
        fprintf(stderr,
                "ERROR: modifications via mmap not visible to read()\n");
    } else {
        printf("modifications via mmap visible to read() ✅\n");
    }

    munmap(map, len);
}

static void test_fork_sharing(int fd)
{
    const char *msg = "fork-sharing-test\n";
    size_t len = strlen(msg);
    ssize_t ret;
    off_t pos;

    printf("\n== fork() + mmap sharing test ==\n");

    /* Reset device position and write fresh data */
    pos = lseek(fd, 0, SEEK_SET);
    if (pos < 0)
        die("lseek");

    ret = write(fd, msg, len);
    if (ret < 0)
        die("write");
    if ((size_t)ret != len) {
        fprintf(stderr, "short write in fork test: %zd\n", ret);
        exit(EXIT_FAILURE);
    }

    void *map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        die("mmap");

    printf("parent: initial mmap contents: \"%.*s\"\n",
           (int)len, (char *)map);

    pid_t pid = fork();
    if (pid < 0)
        die("fork");

    if (pid == 0) {
        /* Child: modify the mapped data */
        const char *child_msg = "CHILD\n";
        size_t child_len = strlen(child_msg);

        printf("child: writing \"%s\" into mmap region\n", child_msg);
        memcpy(map, child_msg, child_len);

        /* Ensure it's visible in this process */
        printf("child: mmap now: \"%.*s\"\n",
               (int)len, (char *)map);

        _exit(0);
    } else {
        /* Parent */
        int status;
        waitpid(pid, &status, 0);
        printf("parent: after child exit, mmap contents: \"%.*s\"\n",
               (int)len, (char *)map);

        printf("If this shows \"CHILD\" at the beginning, "
               "the mapping is shared ✅\n");

        munmap(map, len);
    }
}

static void sigbus_handler(int sig)
{
    (void)sig;
    fprintf(stderr, "Got SIGBUS (likely accessed beyond dev->size)\n");
    _exit(1);
}

static void test_sigbus_beyond_size(int fd)
{
    printf("\n== SIGBUS beyond dev->size test (optional) ==\n");

    /* install SIGBUS handler so we can see it nicely */
    struct sigaction sa = {
        .sa_handler = sigbus_handler,
    };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGBUS, &sa, NULL) < 0)
        die("sigaction");

    /* write a small amount */
    const char *msg = "short\n";
    size_t len = strlen(msg);
    if (lseek(fd, 0, SEEK_SET) < 0)
        die("lseek");
    if (write(fd, msg, len) != (ssize_t)len)
        die("write");

    /* map more bytes than we actually wrote */
    size_t map_len = 4096; /* one page */
    char *map = mmap(NULL, map_len, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        die("mmap");

    printf("mapped %zu bytes; dev->size is only ~%zu bytes (from our write)\n",
           map_len, len);

    printf("accessing map[%zu] (within size) ...\n", len - 1);
    (void)map[len - 1]; /* should be fine */

    printf("accessing map[%zu] (beyond dev->size) ...\n", len + 4090);
    /* This *should* cause SIGBUS due to your VM_FAULT_SIGBUS path */
    (void)map[len + 4090];

    /* If we somehow get here, SIGBUS did not occur */
    printf("WARNING: didn't get SIGBUS; behaviour may differ from expected\n");
    munmap(map, map_len);
}

int main(void)
{
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("open /dev/scullv0");
        fprintf(stderr,
                "Make sure your module is loaded and device node %s exists.\n",
                dev_path);
        return EXIT_FAILURE;
    }

    test_basic_mmap(fd);
    test_fork_sharing(fd);
    test_sigbus_beyond_size(fd);

    close(fd);
    return 0;
}
