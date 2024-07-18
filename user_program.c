// user_program.c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int main() {
    int ret, fd;
    char buffer[256];

    fd = open("/dev/simplechar", O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device...");
        return errno;
    }

    ret = read(fd, buffer, 256);
    if (ret < 0) {
        perror("Failed to read the message from the device.");
        return errno;
    }
    printf("The received message is: [%s]\n", buffer);

    close(fd);
    return 0;
}

