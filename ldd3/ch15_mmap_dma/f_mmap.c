#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main() {
    const char *filename = "testfile.bin";
    int fd = open(filename, O_RDWR | O_CREAT, 0666);
    write(fd, "HELLO", 5); // write some initial data
    // Map the file with MAP_SHARED
    char *shared_map = mmap(NULL, 5, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    // Map the file with MAP_PRIVATE
    char *private_map = mmap(NULL, 5, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
    // Modify both mappings:
    shared_map[0] = 'Y'; // modify shared mapping
    private_map[1] = 'Z'; // modify private mapping
    // Flush changes of shared mapping to disk:
    msync(shared_map, 5, MS_SYNC);
    // Read file content from disk:
    lseek(fd, 0, SEEK_SET);
    char buf[6] = {0};
    read(fd, buf, 5);
    printf("File contents on disk: %s\n", buf);
    munmap(shared_map, 5);
    munmap(private_map, 5);
    close(fd);
    return 0;
}