#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEFAULT_PORT 80
#define BUFFER_SIZE 4096
#define STATIC_DIR "./static"

// Global variables for tracking stats
int total_requests = 0;
int total_received_bytes = 0;
int total_sent_bytes = 0;
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

void handle_client(int client_socket);
void send_404(int client_socket);
void send_stats(int client_socket);
void send_file(int client_socket, const char *path);
void handle_calc(int client_socket, const char *query);

void *client_thread(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);
    handle_client(client_socket);
    close(client_socket);
    return NULL;
}

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    int received = recv(client_socket, buffer, BUFFER_SIZE, 0);

    if (received <= 0) 
        return;

    pthread_mutex_lock(&stats_lock);
    total_requests++;
    total_received_bytes += received;
    pthread_mutex_unlock(&stats_lock);

    buffer[received] = '\0';

    // Routing requests based on the URL path
    if (strncmp(buffer, "GET /stats", 10) == 0) {
        send_stats(client_socket);
    } 
    else if (strncmp(buffer, "GET /static/", 12) == 0) {
        char *file_path = buffer + 5;
        send_file(client_socket, file_path);
    } 
    else if (strncmp(buffer, "GET /calc?", 10) == 0) {
        handle_calc(client_socket, buffer + 10);
    } 
    else {
        send_404(client_socket);
    }
}

void send_404(int client_socket) {
    const char *response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: 58\r\n\r\n"
                           "<html><body><h1>404 Not Found</h1><p>The requested page could not be found.</p></body></html>";
    send(client_socket, response, strlen(response), 0);
}

void send_stats(int client_socket) {
    char response[BUFFER_SIZE];
    int content_length;

    pthread_mutex_lock(&stats_lock);
    content_length = snprintf(response, BUFFER_SIZE, 
        "<html><head><title>Server Statistics</title></head><body>"
        "<h1>Server Statistics</h1>"
        "<p><strong>Total Requests:</strong> %d</p>"
        "<p><strong>Total Bytes Received:</strong> %d bytes</p>"
        "<p><strong>Total Bytes Sent:</strong> %d bytes</p>"
        "</body></html>",
        total_requests, total_received_bytes, total_sent_bytes);
    pthread_mutex_unlock(&stats_lock);

    char header[BUFFER_SIZE];
    snprintf(header, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", content_length);

    send(client_socket, header, strlen(header), 0);
    send(client_socket, response, content_length, 0);

    pthread_mutex_lock(&stats_lock);
    total_sent_bytes += content_length + strlen(header);
    pthread_mutex_unlock(&stats_lock);
}

void send_file(int client_socket, const char *path) {
    char file_path[BUFFER_SIZE];
    snprintf(file_path, BUFFER_SIZE, "%s%s", STATIC_DIR, path + 7);

    int file = open(file_path, O_RDONLY);
    if (file == -1) {
        send_404(client_socket);
        return;
    }

    struct stat file_stat;
    fstat(file, &file_stat);
    int file_size = file_stat.st_size;

    char header[BUFFER_SIZE];
    snprintf(header, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", file_size);
    send(client_socket, header, strlen(header), 0);

    int bytes_read;
    while ((bytes_read = read(file, file_path, BUFFER_SIZE)) > 0) {
        send(client_socket, file_path, bytes_read, 0);
        pthread_mutex_lock(&stats_lock);
        total_sent_bytes += bytes_read;
        pthread_mutex_unlock(&stats_lock);
    }

    close(file);
}

void handle_calc(int client_socket, const char *query) {
    int a = 0, b = 0;
    sscanf(query, "a=%d&b=%d", &a, &b);
    int sum = a + b;

    char response[BUFFER_SIZE];
    int content_length = snprintf(response, BUFFER_SIZE, 
        "<html><head><title>Calculation Result</title></head><body>"
        "<h1>Calculation Result</h1>"
        "<p><strong>Sum of %d and %d:</strong> %d</p>"
        "</body></html>", a, b, sum);

    char header[BUFFER_SIZE];
    snprintf(header, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", content_length);

    send(client_socket, header, strlen(header), 0);
    send(client_socket, response, content_length, 0);

    pthread_mutex_lock(&stats_lock);
    total_sent_bytes += content_length + strlen(header);
    pthread_mutex_unlock(&stats_lock);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_socket, 5);

    printf("Server lestening on port %d\n", port);

    while (1) {
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(server_socket, NULL, NULL);

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, client_socket);
        pthread_detach(tid);
    }

    close(server_socket);
    return 0;
}
