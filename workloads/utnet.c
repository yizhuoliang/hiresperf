#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 7891
#define BUFFER_SIZE 1024  // Buffer size for each send (1 KB)
#define TOTAL_DATA 100 * 1024 * 1024  // 100 MB of data

void *thread_function(void *arg);

int main() {
    pthread_t thread_id;
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

    // Create socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Fill server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Set up destination address for sending data
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &cliaddr.sin_addr);

    // Create thread
    pthread_create(&thread_id, NULL, thread_function, (void *)&sockfd);
    pthread_detach(thread_id);

    printf("Main thread PID: %d\n", getpid());

    // Send data
    char buffer[BUFFER_SIZE];
    memset(buffer, 'A', BUFFER_SIZE);  // Fill the buffer with 'A's

    long bytes_sent = 0;
    int len = sizeof(cliaddr);
    while (bytes_sent < TOTAL_DATA) {
        int sent = sendto(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, len);
        if (sent < 0) {
            perror("Send failed");
            break;
        }
        bytes_sent += sent;
    }

    printf("Total bytes sent: %ld\n", bytes_sent);

    // Close the socket and exit
    close(sockfd);
    pthread_exit(NULL);
    return 0;
}

void *thread_function(void *arg) {
    int sockfd = *((int *)arg);
    struct sockaddr_in cliaddr;
    int len = sizeof(cliaddr);

    printf("Worker thread PID: %d\n", getpid());

    // Receive data
    char buffer[BUFFER_SIZE];
    long total_received = 0;
    while (total_received < TOTAL_DATA) {
        int received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
        if (received < 0) {
            perror("Receive failed");
            break;
        }
        total_received += received;
    }

    printf("Total bytes received: %ld\n", total_received);
    return NULL;
}
