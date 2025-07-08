#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const int PORT = 8080;
const int INITIAL_BUFFER_SIZE = 8192;    // Start with 8KB
const int MAX_BUFFER_SIZE = 1024 * 1024; // Max 1MB

void exitter(const char *err);

int main()
{
    int sock = 0;
    struct sockaddr_in serv_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        exitter("Socket creation error");
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        exitter("Invalid address / Address not supported");
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        exitter("Connection Failed");
    }

    printf("Connected to server successfully.\n");

    char command[1024];
    printf("Enter command to execute remotely: ");
    fgets(command, sizeof(command), stdin);
    command[strcspn(command, "\n")] = 0;

    if (send(sock, command, strlen(command) + 1, 0) < 0)
    {
        exitter("Failed to send command");
    }

    // Dynamic buffer allocation
    int buffer_size = INITIAL_BUFFER_SIZE;
    char *full_output = malloc(buffer_size);
    if (!full_output)
    {
        exitter("Failed to allocate memory");
    }

    char recv_buffer[4096];
    ssize_t n;
    int total_len = 0;

    printf("Receiving output...\n");

    // Read all output until server closes connection
    while ((n = recv(sock, recv_buffer, sizeof(recv_buffer), 0)) > 0)
    {
        printf("[client] Received %zd bytes from server\n", n);

        // Check if we need to resize buffer
        if (total_len + n > buffer_size)
        {
            // Double the buffer size
            int new_size = buffer_size * 2;
            if (new_size > MAX_BUFFER_SIZE)
            {
                fprintf(stderr, "Output too large (exceeds %d bytes)\n", MAX_BUFFER_SIZE);
                free(full_output);
                close(sock);
                return 1;
            }

            char *new_buffer = realloc(full_output, new_size);
            if (!new_buffer)
            {
                fprintf(stderr, "Failed to reallocate memory\n");
                free(full_output);
                close(sock);
                return 1;
            }

            full_output = new_buffer;
            buffer_size = new_size;
            printf("[client] Expanded buffer to %d bytes\n", buffer_size);
        }

        memcpy(full_output + total_len, recv_buffer, n);
        total_len += n;
    }

    printf("[client] Total received: %d bytes\n", total_len);

    if (total_len < 4)
    {
        fprintf(stderr, "Did not receive enough data for exit status\n");
        free(full_output);
        close(sock);
        return 1;
    }

    // Extract exit status (last 4 bytes)
    int exit_status_network;
    memcpy(&exit_status_network, full_output + total_len - 4, 4);
    int exit_status = ntohl(exit_status_network);

    printf("\n=== COMMAND OUTPUT ===\n");
    if (total_len > 4)
    {
        // Write output excluding the last 4 bytes (exit status)
        fwrite(full_output, 1, total_len - 4, stdout);
        fflush(stdout);
    }
    else
    {
        printf("[client] No output received from server.\n");
    }
    printf("\n=== END OUTPUT ===\n");
    printf("Exit status: %d\n", exit_status);

    free(full_output);
    close(sock);
    return 0;
}

void exitter(const char *err)
{
    perror(err);
    exit(EXIT_FAILURE);
}