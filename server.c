#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <json-c/json.h>
#include <curl/curl.h>        // Add this line
#include <json-c/json.h> 

#define PORT 8080
#define BUFFER_SIZE 2048
#define MAX_CLIENTS 10
#define MAX_USERS 100
#define UPLOAD_DIR "uploads"
#define USER_DB_FILE "users.db"

// FIXED: Proper array declarations
typedef struct {
    char username[50];      // Array of 50 chars
    char password[100];     // Array of 100 chars (not single char)
    int is_online;
    time_t last_seen;
} user_account_t;

// FIXED: Proper array declaration for username
typedef struct {
    int socket;
    int id;
    char username[50];      // Array of 50 chars (not single char)
    int is_authenticated;
    struct sockaddr_in address;
} client_t;

struct http_response {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
char* ask_gpt2_faq(const char* question);

client_t *clients[MAX_CLIENTS];
user_account_t users[MAX_USERS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
int client_count = 0;
int user_count = 0;

// Simple hash function
unsigned long simple_hash(char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

// Load users from file
void load_users() {
    FILE *fp = fopen(USER_DB_FILE, "r");
    if (fp == NULL) {
        printf("No user database found. Starting fresh.\n");
        return;
    }
    
    while (fscanf(fp, "%49s %99s %d %ld", 
                  users[user_count].username, 
                  users[user_count].password,
                  &users[user_count].is_online,
                  &users[user_count].last_seen) == 4) {
        users[user_count].is_online = 0;
        user_count++;
        if (user_count >= MAX_USERS) break;
    }
    fclose(fp);
    printf("Loaded %d users from database.\n", user_count);
}

// Save users to file
void save_users() {
    FILE *fp = fopen(USER_DB_FILE, "w");
    if (fp == NULL) {
        perror("Failed to save user database");
        return;
    }
    
    for (int i = 0; i < user_count; i++) {
        fprintf(fp, "%s %s %d %ld\n", 
                users[i].username, 
                users[i].password,
                users[i].is_online,
                users[i].last_seen);
    }
    fclose(fp);
}

// Find user by username
int find_user(char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

// Register new user
int register_user(char *username, char *password) {
    pthread_mutex_lock(&users_mutex);
    
    if (find_user(username) != -1) {
        pthread_mutex_unlock(&users_mutex);
        return 0;
    }
    
    if (user_count >= MAX_USERS) {
        pthread_mutex_unlock(&users_mutex);
        return -1;
    }
    
    strcpy(users[user_count].username, username);
    sprintf(users[user_count].password, "%lu", simple_hash(password));
    users[user_count].is_online = 0;
    users[user_count].last_seen = time(NULL);
    user_count++;
    
    save_users();
    pthread_mutex_unlock(&users_mutex);
    return 1;
}

// Authenticate user
int authenticate_user(char *username, char *password) {
    pthread_mutex_lock(&users_mutex);
    
    int user_idx = find_user(username);
    if (user_idx == -1) {
        pthread_mutex_unlock(&users_mutex);
        return 0;
    }
    
    char hashed_pass[100];
    sprintf(hashed_pass, "%lu", simple_hash(password));
    
    if (strcmp(users[user_idx].password, hashed_pass) == 0) {
        users[user_idx].is_online = 1;
        users[user_idx].last_seen = time(NULL);
        save_users();
        pthread_mutex_unlock(&users_mutex);
        return 1;
    }
    
    pthread_mutex_unlock(&users_mutex);
    return 0;
}

// Logout user
void logout_user(char *username) {
    pthread_mutex_lock(&users_mutex);
    int user_idx = find_user(username);
    if (user_idx != -1) {
        users[user_idx].is_online = 0;
        users[user_idx].last_seen = time(NULL);
        save_users();
    }
    pthread_mutex_unlock(&users_mutex);
}

// Find client by username
client_t* find_client_by_username(char *username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && strcmp(clients[i]->username, username) == 0) {
            return clients[i];
        }
    }
    return NULL;
}

// Add client
void add_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == NULL) {
            clients[i] = client;
            client_count++;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Remove client
void remove_client(int id) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->id == id) {
            if (clients[i]->is_authenticated) {
                logout_user(clients[i]->username);
            }
            clients[i] = NULL;
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Send message to all authenticated clients
void send_message_to_all(char *message, int sender_id) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->id != sender_id && clients[i]->is_authenticated) {
            if (send(clients[i]->socket, message, strlen(message), 0) < 0) {
                perror("Failed to send message");
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Handle private message
void handle_private_message(int sender_id, char* target_user, char* message) {
    client_t *sender = NULL;
    client_t *target = NULL;
    
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->id == sender_id) {
            sender = clients[i];
            break;
        }
    }
    
    target = find_client_by_username(target_user);
    pthread_mutex_unlock(&clients_mutex);
    
    if (!sender || !sender->is_authenticated) {
        char error_msg[] = "Error: You must be logged in to send private messages";
        send(sender_id, error_msg, strlen(error_msg), 0);
        return;
    }
    
    if (!target || !target->is_authenticated) {
        char error_msg[200];
        snprintf(error_msg, sizeof(error_msg), "Error: User '%s' not found or offline", target_user);
        send(sender->socket, error_msg, strlen(error_msg), 0);
        return;
    }
    
    char private_msg[BUFFER_SIZE + 100];
    snprintf(private_msg, sizeof(private_msg), "[PRIVATE] %s: %s", sender->username, message);
    send(target->socket, private_msg, strlen(private_msg), 0);
    
    char confirm_msg[200];
    snprintf(confirm_msg, sizeof(confirm_msg), "Private message sent to %s", target_user);
    send(sender->socket, confirm_msg, strlen(confirm_msg), 0);
    
    printf("Private message from %s to %s: %s\n", sender->username, target_user, message);
}

// List online users
void list_online_users(int client_socket) {
    char user_list[BUFFER_SIZE] = "Online users: ";
    int first = 1;
    
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->is_authenticated) {
            if (!first) strcat(user_list, ", ");
            strcat(user_list, clients[i]->username);
            first = 0;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    if (first) {
        strcpy(user_list, "No users online");
    }
    
    send(client_socket, user_list, strlen(user_list), 0);
}

// File handling functions (unchanged)
void handle_file_put(int client_socket, char *filename) {
    long file_size;
    
    if (recv(client_socket, &file_size, sizeof(file_size), 0) <= 0) {
        printf("Failed to receive file size\n");
        return;
    }
    file_size = ntohl(file_size);
    
    if (file_size < 0) {
        printf("Client reported file not found: %s\n", filename);
        char response[] = "File not found on client side";
        send(client_socket, response, strlen(response), 0);
        return;
    }
    
    mkdir(UPLOAD_DIR, 0777);
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", UPLOAD_DIR, filename);
    
    FILE *fp = fopen(filepath, "wb");
    if (fp == NULL) {
        perror("Failed to create file");
        char response[] = "Server: Failed to create file";
        send(client_socket, response, strlen(response), 0);
        return;
    }
    
    char buffer[BUFFER_SIZE];
    long total_received = 0;
    ssize_t bytes_received;
    
    while (total_received < file_size) {
        bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) break;
        fwrite(buffer, 1, bytes_received, fp);
        total_received += bytes_received;
    }
    fclose(fp);
    
    if (total_received == file_size) {
        printf("File '%s' uploaded successfully (%ld bytes)\n", filename, file_size);
        char response[256];
        snprintf(response, sizeof(response), "Server: File '%s' uploaded successfully", filename);
        send(client_socket, response, strlen(response), 0);
    } else {
        printf("File upload failed. Expected %ld, got %ld\n", file_size, total_received);
        char response[] = "Server: File upload failed";
        send(client_socket, response, strlen(response), 0);
    }
}

void handle_file_get(int client_socket, char *filename) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", UPLOAD_DIR, filename);
    
    FILE *fp = fopen(filepath, "rb");
    long file_size;
    
    if (fp == NULL) {
        file_size = -1;
        long net_size = htonl(file_size);
        send(client_socket, &net_size, sizeof(net_size), 0);
        printf("File '%s' not found for download\n", filename);
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    long net_size = htonl(file_size);
    send(client_socket, &net_size, sizeof(net_size), 0);
    
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        if (send(client_socket, buffer, bytes_read, 0) < 0) {
            perror("Failed to send file chunk");
            break;
        }
    }
    fclose(fp);
    
    printf("File '%s' sent to client (%ld bytes)\n", filename, file_size);
}

void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE + 100];
    ssize_t bytes_received;
    
    printf("Client %d connected from %s:%d\n", 
           client->id, 
           inet_ntoa(client->address.sin_addr), 
           ntohs(client->address.sin_port));
    
    char auth_prompt[] = "Welcome! Please login or register.\nCommands: /login <username> <password> or /register <username> <password>";
    send(client->socket, auth_prompt, strlen(auth_prompt), 0);
    
    while ((bytes_received = recv(client->socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        
        if (strncmp(buffer, "/login ", 7) == 0) {
            char *username = strtok(buffer + 7, " ");
            char *password = strtok(NULL, " ");
            
            if (username && password) {
                if (find_client_by_username(username)) {
                    char error_msg[] = "Error: User already logged in";
                    send(client->socket, error_msg, strlen(error_msg), 0);
                } else if (authenticate_user(username, password)) {
                    strcpy(client->username, username);
                    client->is_authenticated = 1;
                    char success_msg[] = "Login successful! You can now chat, send files, or use commands.";
                    send(client->socket, success_msg, strlen(success_msg), 0);
                    
                    snprintf(message, sizeof(message), "%s joined the chat", username);
                    send_message_to_all(message, client->id);
                    printf("User %s logged in\n", username);
                } else {
                    char error_msg[] = "Login failed: Invalid username or password";
                    send(client->socket, error_msg, strlen(error_msg), 0);
                }
            } else {
                char error_msg[] = "Usage: /login <username> <password>";
                send(client->socket, error_msg, strlen(error_msg), 0);
            }
        }
        else if (strncmp(buffer, "/register ", 10) == 0) {
            char *username = strtok(buffer + 10, " ");
            char *password = strtok(NULL, " ");
            
            if (username && password) {
                int result = register_user(username, password);
                if (result == 1) {
                    char success_msg[] = "Registration successful! You can now login.";
                    send(client->socket, success_msg, strlen(success_msg), 0);
                    printf("New user registered: %s\n", username);
                } else if (result == 0) {
                    char error_msg[] = "Registration failed: Username already exists";
                    send(client->socket, error_msg, strlen(error_msg), 0);
                } else {
                    char error_msg[] = "Registration failed: Server full";
                    send(client->socket, error_msg, strlen(error_msg), 0);
                }
            } 

else if (strncmp(buffer, "/faq ", 5) == 0) {
    char *question = buffer + 5;
    if (strlen(question) > 0) {
        printf("Client %s asked FAQ: %s\n", client->username, question);
        
        // Try to get answer from GPT-2 service
        char *gpt_answer = ask_gpt2_faq(question);
        
        if (gpt_answer && strlen(gpt_answer) > 0) {
            send(client->socket, gpt_answer, strlen(gpt_answer), 0);
            free(gpt_answer);
        } else {
            // Fallback to simple responses if service fails
            char response[1000];
            if (strstr(question, "run") != NULL) {
                strcpy(response, "FAQ Bot: To run this project:\n1. gcc server.c -o server -lpthread -lcurl -ljson-c\n2. gcc client.c -o client -lpthread\n3. ./server\n4. ./client 127.0.0.1");
            } else if (strstr(question, "difficulty") != NULL) {
                strcpy(response, "FAQ Bot: Difficulty: Intermediate C programming. Needs: sockets, threading, file I/O knowledge.");
            } else if (strstr(question, "features") != NULL) {
                strcpy(response, "FAQ Bot: Features: Multi-threading, authentication, private messages, file transfer, 50 concurrent users.");
            } else {
                strcpy(response, "FAQ Bot: I'm a smart assistant! Try asking about the project, general questions, or say hello!");
            }
            send(client->socket, response, strlen(response), 0);
        }
        
    } else {
        char help_msg[] = "Usage: /faq <question>\nTry: /faq how to run, /faq how are you";
        send(client->socket, help_msg, strlen(help_msg), 0);
    }
}
else {
                char error_msg[] = "Usage: /register <username> <password>";
                send(client->socket, error_msg, strlen(error_msg), 0);
            }
        }
        else if (!client->is_authenticated) {
            char error_msg[] = "Please login first using /login <username> <password>";
            send(client->socket, error_msg, strlen(error_msg), 0);
        }
        else if (strncmp(buffer, "/msg ", 5) == 0) {
            char *target_user = strtok(buffer + 5, " ");
            char *msg_content = strtok(NULL, "");
            
            if (target_user && msg_content) {
                handle_private_message(client->id, target_user, msg_content);
            } else {
                char error_msg[] = "Usage: /msg <username> <message>";
                send(client->socket, error_msg, strlen(error_msg), 0);
            }
        }
        else if (strcmp(buffer, "/users") == 0) {
            list_online_users(client->socket);
        }
        // Add this AFTER your existing command handlers
else if (strncmp(buffer, "/faq ", 5) == 0) {
    char *question = buffer + 5;
    if (strlen(question) > 0) {
        printf("Client %s asked FAQ: %s\n", client->username, question);
        
        // Try GPT-2 service first
        char *gpt_answer = ask_gpt2_faq(question);
        
        if (gpt_answer) {
            printf("GPT-2 response: %s\n", gpt_answer);
            send(client->socket, gpt_answer, strlen(gpt_answer), 0);
            free(gpt_answer);
        } else {
            // Fallback to project-specific answers
            printf("GPT-2 service failed, using fallback\n");
            char response[1000];
            if (strstr(question, "run") != NULL) {
                strcpy(response, "FAQ Bot: To run this project:\n1. gcc server.c -o server -lpthread -lcurl -ljson-c\n2. gcc client.c -o client -lpthread\n3. ./server\n4. ./client 127.0.0.1");
            } else if (strstr(question, "you") != NULL || strstr(question, "are") != NULL) {
                strcpy(response, "FAQ Bot: I'm your helpful chat server assistant! Ask me anything about the project or general questions.");
            } else if (strstr(question, "joke") != NULL) {
                strcpy(response, "FAQ Bot: Why do programmers prefer dark mode? Because light attracts bugs! 🐛");
            } else {
                strcpy(response, "FAQ Bot: Service temporarily unavailable. Try asking about 'how to run', or say hello!");
            }
            send(client->socket, response, strlen(response), 0);
        }
    } else {
        char help_msg[] = "Usage: /faq <question>\nTry: /faq how are you, /faq tell me a joke";
        send(client->socket, help_msg, strlen(help_msg), 0);
    }
}

        else if (strncmp(buffer, "put ", 4) == 0) {
            char *filename = buffer + 4;
            printf("User %s wants to upload file: %s\n", client->username, filename);
            handle_file_put(client->socket, filename);
        }
        else if (strncmp(buffer, "get ", 4) == 0) {
            char *filename = buffer + 4;
            printf("User %s wants to download file: %s\n", client->username, filename);
            handle_file_get(client->socket, filename);
        }
        else if (strcmp(buffer, "exit") == 0) {
            printf("User %s disconnected\n", client->username);
            break;
        }
        else {
            printf("%s: %s\n", client->username, buffer);
            snprintf(message, sizeof(message), "%s: %s", client->username, buffer);
            send_message_to_all(message, client->id);
        }
    }
    
    if (client->is_authenticated) {
        snprintf(message, sizeof(message), "%s left the chat", client->username);
        send_message_to_all(message, client->id);
    }
    
    close(client->socket);
    remove_client(client->id);
    free(client);
    pthread_exit(NULL);
}


int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = NULL;
    }
    
    load_users();
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d\n", PORT);
    printf("Upload directory: %s\n", UPLOAD_DIR);
    printf("User database: %s\n", USER_DB_FILE);
    printf("Waiting for clients...\n");
    
    mkdir(UPLOAD_DIR, 0777);
    
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        if (client_count >= MAX_CLIENTS) {
            printf("Maximum clients reached. Rejecting new connection.\n");
            close(client_socket);
            continue;
        }
        
        client_t *client = (client_t*)malloc(sizeof(client_t));
        client->socket = client_socket;
        client->address = client_addr;
        client->id = client_socket;
        client->is_authenticated = 0;
        strcpy(client->username, "");
        
        add_client(client);
        
        if (pthread_create(&thread_id, NULL, handle_client, (void*)client) != 0) {
            perror("Failed to create thread");
            free(client);
            close(client_socket);
        } else {
            pthread_detach(thread_id);
        }
    }
    
    close(server_socket);
    return 0;
}
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct http_response *mem = (struct http_response *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

char* ask_gpt2_faq(const char* question) {
    CURL *curl;
    CURLcode res;
    struct http_response chunk;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    printf("DEBUG: Connecting to GPT-2 service on Windows...\n");
    
    curl = curl_easy_init();
    if (curl) {
        char payload[1024];
        snprintf(payload, sizeof(payload), "{\"question\": \"%s\"}", question);
        printf("DEBUG: Sending: %s\n", payload);
        
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        // ONLY use Windows IP (since we know it works)
        curl_easy_setopt(curl, CURLOPT_URL, "http://10.14.94.221:5005/faq");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        
        res = curl_easy_perform(curl);
        
        if (res == CURLE_OK && chunk.memory) {
            printf("DEBUG: Success! Raw response: %.200s\n", chunk.memory);
            
            // Parse JSON response
            char *answer_start = strstr(chunk.memory, "\"answer\":\"");
            if (answer_start) {
                answer_start += 10; // Skip "answer":"
                char *answer_end = strstr(answer_start, "\"}");
                if (!answer_end) {
                    answer_end = strchr(answer_start, '"');
                }
                
                if (answer_end) {
                    size_t answer_len = answer_end - answer_start;
                    if (answer_len > 400) answer_len = 400; // Limit length
                    
                    char *result = malloc(answer_len + 1);
                    strncpy(result, answer_start, answer_len);
                    result[answer_len] = '\0';
                    
                    printf("DEBUG: Parsed answer: %s\n", result);
                    
                    free(chunk.memory);
                    curl_easy_cleanup(curl);
                    curl_slist_free_all(headers);
                    return result;
                }
            }
        } else {
            printf("DEBUG: CURL failed: %s\n", curl_easy_strerror(res));
        }
        
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    
    if (chunk.memory) free(chunk.memory);
    return NULL;
}
