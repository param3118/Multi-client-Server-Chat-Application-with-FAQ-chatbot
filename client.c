#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 2048
#define DOWNLOAD_DIR "downloads"

int sock = 0;

void *receive_handler(void *socket_desc);

void handle_file_put(char* filename) {
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "put %s", filename);
    send(sock, command, strlen(command), 0);

    FILE *fp = fopen(filename, "rb");
    long file_size;

    if (fp == NULL) {
        file_size = -1;
        long net_size = htonl(file_size);
        send(sock, &net_size, sizeof(net_size), 0);
        printf("Client: File '%s' not found.\n", filename);
        return;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    long net_size = htonl(file_size);
    send(sock, &net_size, sizeof(net_size), 0);
    
    char buffer[BUFFER_SIZE] = {0};
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            perror("Failed to send file chunk");
            break;
        }
    }
    fclose(fp);
}

void handle_file_get(char* filename) {
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "get %s", filename);
    send(sock, command, strlen(command), 0);
    
    char buffer[BUFFER_SIZE];
    long file_size;

    if (recv(sock, &file_size, sizeof(file_size), 0) <= 0) {
        printf("Server disconnected or error occurred.\n");
        return;
    }
    file_size = ntohl(file_size);

    if (file_size < 0) {
        printf("Server: File '%s' not found.\n", filename);
        return;
    }

    mkdir(DOWNLOAD_DIR, 0777);
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", DOWNLOAD_DIR, filename);
    
    FILE *fp = fopen(filepath, "wb");
    if (fp == NULL) {
        perror("Client: Failed to create file");
        return;
    }
    
    long total_received = 0;
    ssize_t bytes_received;
    while (total_received < file_size) {
        bytes_received = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) break;
        fwrite(buffer, 1, bytes_received, fp);
        total_received += bytes_received;
    }
    fclose(fp);

    if (total_received == file_size) {
        printf("File '%s' downloaded successfully to '%s' folder.\n", filename, DOWNLOAD_DIR);
    } else {
        printf("File download failed. Expected %ld, got %ld\n", file_size, total_received);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_address;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = inet_addr(argv[1]);

    if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to server!\n");
    printf("Commands:\n");
    printf("  /login <username> <password>  - Login to your account\n");
    printf("  /register <username> <password> - Create new account\n");
    printf("  /msg <username> <message>     - Send private message\n");
    printf("  /users                        - List online users\n");
    printf("  put <filename>                - Upload file\n");
    printf("  get <filename>                - Download file\n");
    printf("  exit                          - Quit\n");

    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, receive_handler, NULL) < 0) {
        perror("Could not create receiver thread");
        exit(EXIT_FAILURE);
    }

    char message[BUFFER_SIZE];
    while (1) {
        printf("> ");
        if (fgets(message, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        message[strcspn(message, "\n")] = 0;

        if (strlen(message) == 0) continue;

        if (strncmp(message, "put ", 4) == 0) {
            handle_file_put(message + 4);
        } else if (strncmp(message, "get ", 4) == 0) {
            pthread_cancel(recv_thread);
            pthread_join(recv_thread, NULL);

            handle_file_get(message + 4);

            if (pthread_create(&recv_thread, NULL, receive_handler, NULL) < 0) {
                perror("Could not restart receiver thread");
                break;
            }
        } else {
             if (send(sock, message, strlen(message), 0) < 0) {
                perror("Send failed");
                break;
            }
             if (strcmp(message, "exit") == 0) {
                printf("Closing connection...\n");
                break;
            }
        }
    }

    pthread_cancel(recv_thread);
    pthread_join(recv_thread, NULL);
    close(sock);
    return 0;
}

void *receive_handler(void *socket_desc) {
    char server_reply[BUFFER_SIZE];
    ssize_t bytes_received;

    while ((bytes_received = recv(sock, server_reply, BUFFER_SIZE, 0)) > 0) {
        server_reply[bytes_received] = '\0';
        printf("\r%s\n> ", server_reply);
        fflush(stdout);
    }

    if (bytes_received == 0) {
        printf("\rServer disconnected. Press Enter to exit.\n");
        fflush(stdout);
        close(sock);
        exit(0);
    }
    return NULL;
}
