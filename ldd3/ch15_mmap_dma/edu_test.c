// edu_test.c - userspace tester for /dev/edu
// Compile:  gcc -Wall -O2 edu_test.c -o edu_test
// Run (as root):  sudo ./edu_test <mode>
//
//   mode 1: read/write DMA loopback
//   mode 2: mmap coherent buffer + DMA_TX/DMA_RX loopback
//   mode 3: streaming SG DMA TX-only (RAM->DEV), just exercises GUP/SG path
//   mode 4: streaming SG DMA RX test (DEV->RAM):
//           - coherent buffer -> DEV via EDU_IOC_DMA_TX
//           - DEV -> SG user buffer via EDU_IOC_STREAM_DMA
//           - compare SG user buffer with coherent buffer
//
// NOTE: The EDU hardware has only a single 4KiB DMA buffer at
// EDU_BAR0_DMA_BUFFER_REG. It does NOT have a true SG engine or FIFO,
// so we do NOT try to do a full "big streaming loopback" via SG on both
// sides. Mode 4 only tests SG on the host side (user pages) with the
// device's single 4KiB buffer.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "edu_ioctl.h"

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static int open_edu(void)
{
    int fd = open("/dev/edu", O_RDWR);
    if (fd < 0)
        die("open /dev/edu");
    return fd;
}

/* Simple helpers */

static void fill_pattern(uint8_t *buf, size_t len, uint8_t base)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(base + (i & 0x0f));
}

static int compare_buffers(const uint8_t *a, const uint8_t *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            fprintf(stderr,
                    "mismatch at %zu: got 0x%02x, expected 0x%02x\n",
                    i, a[i], b[i]);
            return -1;
        }
    }
    return 0;
}

/* ========== MODE 1: read/write loopback via driver read/write ========== */

static void test_rw_loopback(int fd)
{
    const char *msg = "hello from userspace via read/write DMA\n";
    size_t len = strlen(msg) + 1; // include '\0'

    if (len > DMA_BUF_SIZE) {
        fprintf(stderr, "Internal test string too big\n");
        return;
    }

    printf("[mode1] writing %zu bytes with write()...\n", len);

    ssize_t w = write(fd, msg, len);
    if (w < 0)
        die("write");
    if ((size_t)w != len) {
        fprintf(stderr, "short write: %zd\n", w);
        return;
    }

    char buf[DMA_BUF_SIZE];

    printf("[mode1] reading %zu bytes with read()...\n", len);

    ssize_t r = read(fd, buf, len);
    if (r < 0)
        die("read");
    if ((size_t)r != len) {
        fprintf(stderr, "short read: %zd\n", r);
        return;
    }

    printf("[mode1] got back: \"%s\"\n", buf);

    if (memcmp(msg, buf, len) == 0)
        printf("[mode1] PASS: read/write DMA loopback match\n");
    else
        printf("[mode1] FAIL: data mismatch\n");
}

/* ========== MODE 2: mmap + DMA_TX/DMA_RX on coherent buffer ========== */

static void test_mmap_dma_loopback(int fd)
{
    printf("[mode2] mmap coherent DMA buffer (offset 0)...\n");

    void *map = mmap(NULL, DMA_BUF_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        die("mmap");

    uint8_t *buf = map;
    size_t len = 512;   // arbitrary < DMA_BUF_SIZE

    // Fill buffer with pattern
    printf("[mode2] filling mapped buffer with pattern...\n");
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(i & 0xff);

    // Tell kernel to DMA RAM -> EDU from coherent buffer
    printf("[mode2] EDU_IOC_DMA_TX of %zu bytes...\n", len);
    __u32 klen = len;
    if (ioctl(fd, EDU_IOC_DMA_TX, &klen) < 0)
        die("ioctl EDU_IOC_DMA_TX");

    // Clear local buffer so we can see RX actually refills it
    memset(buf, 0xAA, len);

    // Tell kernel to DMA EDU -> RAM into coherent buffer
    printf("[mode2] EDU_IOC_DMA_RX of %zu bytes...\n", len);
    if (ioctl(fd, EDU_IOC_DMA_RX, &klen) < 0)
        die("ioctl EDU_IOC_DMA_RX");

    // Check pattern
    int mismatch = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t expected = (uint8_t)(i & 0xff);
        if (buf[i] != expected) {
            fprintf(stderr, "[mode2] mismatch at %zu: got 0x%02x, expected 0x%02x\n",
                    i, buf[i], expected);
            mismatch = 1;
            break;
        }
    }

    if (!mismatch)
        printf("[mode2] PASS: mmap + DMA_TX/DMA_RX loopback match\n");
    else
        printf("[mode2] FAIL: data mismatch\n");

    munmap(map, DMA_BUF_SIZE);
}

/* ========== MODE 3: streaming SG DMA TX-only (exercise path) ========== */

static void test_stream_dma_tx_only(int fd)
{
    /* Allocate a buffer that spans multiple pages and is not page-aligned
     * so we exercise the SG + GUP path properly.
     */
    size_t total   = (3 * 4096) + 123;    // 3 pages + some extra
    size_t misalign = 100;                // offset into malloc() region
    uint8_t *big = malloc(total + misalign);
    if (!big)
        die("malloc");

    uint8_t *buf = big + misalign;
    fill_pattern(buf, total, 0xA0);

    struct edu_stream_desc desc = {
        .user_addr = (uint64_t)(uintptr_t)buf,
        .length    = total,
        .dir       = EDU_DMA_DIR_RAM_TO_DEV,   // TX: user -> device
    };

    printf("[mode3] EDU_IOC_STREAM_DMA TX of %zu bytes from user buffer %p...\n",
           total, buf);

    if (ioctl(fd, EDU_IOC_STREAM_DMA, &desc) < 0)
        die("ioctl EDU_IOC_STREAM_DMA");

    printf("[mode3] EDU_IOC_STREAM_DMA returned success.\n");
    printf("[mode3] (No data verification; this just exercises the SG/GUP TX path.)\n");

    free(big);
}

/* ========== MODE 4: streaming SG DMA RX test (DEV->RAM) ========== */
/*
 * This test *matches the EDU hardware model*:
 *
 *  - Coherent buffer is filled with a pattern and DMA'd to the EDU's
 *    4KiB device buffer using EDU_IOC_DMA_TX.
 *  - We then use EDU_IOC_STREAM_DMA with DEV->RAM into a misaligned,
 *    multi-page user buffer.
 *  - Because each SG segment sources from the same 4KiB device buffer,
 *    we expect the SG user buffer to match the coherent buffer contents
 *    for the requested length (<= DMA_BUF_SIZE).
 */

static void test_stream_dma_rx_from_coherent(int fd)
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        page_size = 4096;

    /* Map the coherent buffer so we can fill it from userspace */
    printf("[mode4] mmap coherent DMA buffer (offset 0)...\n");
    uint8_t *coh = mmap(NULL, DMA_BUF_SIZE,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    if (coh == MAP_FAILED)
        die("mmap coherent");

    /* We'll only test up to DMA_BUF_SIZE bytes because the device buffer
     * is 4KiB. The SG logic is on the host side only.
     */
    size_t len = 2048;  // any value <= DMA_BUF_SIZE and > 1 page to cross pages

    printf("[mode4] filling coherent buffer with pattern...\n");
    fill_pattern(coh, len, 0x10);

    __u32 klen = len;

    /* Coherent -> EDU (device buffer) */
    printf("[mode4] EDU_IOC_DMA_TX of %zu bytes from coherent buffer...\n", len);
    if (ioctl(fd, EDU_IOC_DMA_TX, &klen) < 0)
        die("ioctl EDU_IOC_DMA_TX");

    /* Allocate a misaligned multi-page user RX buffer */
    size_t rx_total = (3 * page_size) + 123;
    size_t misalign = 200;
    uint8_t *big = malloc(rx_total + misalign);
    if (!big)
        die("malloc");

    uint8_t *rx = big + misalign;
    memset(rx, 0xCC, len);   // sentinel

    struct edu_stream_desc desc = {
        .user_addr = (uint64_t)(uintptr_t)rx,
        .length    = len,
        .dir       = EDU_DMA_DIR_DEV_TO_RAM,   // RX: device -> user
    };

    printf("[mode4] EDU_IOC_STREAM_DMA RX (%zu bytes) into SG user buffer %p...\n",
           len, rx);
    if (ioctl(fd, EDU_IOC_STREAM_DMA, &desc) < 0)
        die("ioctl EDU_IOC_STREAM_DMA RX");

    /* Now we expect rx[0..len) to match coh[0..len), since the device buffer
     * contained that pattern from the TX we did just before.
     */
    if (compare_buffers(rx, coh, len) == 0)
        printf("[mode4] PASS: SG DEV->RAM RX buffer matches coherent source\n");
    else
        printf("[mode4] FAIL: SG DEV->RAM RX buffer mismatch\n");

    free(big);
    munmap(coh, DMA_BUF_SIZE);
}

/* ========== main ========== */

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,
                "Usage: %s <mode>\n"
                "  mode 1: read/write DMA loopback\n"
                "  mode 2: mmap + DMA_TX/DMA_RX loopback (coherent buffer)\n"
                "  mode 3: streaming SG DMA TX-only (exercise path)\n"
                "  mode 4: streaming SG DMA RX test (coherent -> DEV -> SG user)\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int mode = atoi(argv[1]);
    int fd = open_edu();

    switch (mode) {
    case 1:
        test_rw_loopback(fd);
        break;
    case 2:
        test_mmap_dma_loopback(fd);
        break;
    case 3:
        test_stream_dma_tx_only(fd);
        break;
    case 4:
        test_stream_dma_rx_from_coherent(fd);
        break;
    default:
        fprintf(stderr, "Unknown mode %d\n", mode);
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd);
    return EXIT_SUCCESS;
}
