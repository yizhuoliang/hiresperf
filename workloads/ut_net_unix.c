#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/udp_socket_path"
#define BUFFER_SIZE 1024  // Buffer size for each send (1 KB)
#define TOTAL_DATA 100 * 1024 * 1024  // 100 MB of data

void *client_function(void *arg);

int main() {
    pthread_t thread_id;
    int server_sockfd, client_sockfd;
    struct sockaddr_un server_addr, client_addr;

    // Remove the old socket path if it exists
    unlink(SOCKET_PATH);

    // Create socket
    server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Bind the socket with the server address
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    listen(server_sockfd, 5);

    // Launch client thread
    pthread_create(&thread_id, NULL, client_function, NULL);
    pthread_detach(thread_id);

    printf("Server thread PID: %d\n", getpid());

    // Accept connection
    int addr_len = sizeof(client_addr);
    client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_sockfd < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }

    // Send data
    char buffer[BUFFER_SIZE];
    memset(buffer, 'A', BUFFER_SIZE);

    long bytes_sent = 0;
    while (bytes_sent < TOTAL_DATA) {
        int sent = send(client_sockfd, buffer, BUFFER_SIZE, 0);
        if (sent < 0) {
            perror("Send failed");
            break;
        }
        bytes_sent += sent;
    }

    printf("Total bytes sent: %ld\n", bytes_sent);

    // Close the sockets and exit
    close(client_sockfd);
    close(server_sockfd);
    unlink(SOCKET_PATH);
    pthread_exit(NULL);
    return 0;
}

void *client_function(void *arg) {
    int sockfd;
    struct sockaddr_un addr;

    printf("Client thread PID: %d\n", getpid());

    // Create socket
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Connect failed");
        exit(EXIT_FAILURE);
    }

    // Receive data
    char buffer[BUFFER_SIZE];
    long total_received = 0;
    while (total_received < TOTAL_DATA) {
        int received = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (received < 0) {
            perror("Receive failed");
            break;
        }
        total_received += received;
    }

    printf("Total bytes received: %ld\n", total_received);

    // Close the socket
    close(sockfd);
    return NULL;
}
