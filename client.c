#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

const int PORT = 8080;

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

    char output_buffer[1024];
    ssize_t n;
    char exit_status_buffer[4];
    int exit_status_index = 0;
    int got_exit_status = 0;

    printf("Command output:\n");

    while ((n = recv(sock, output_buffer, sizeof(output_buffer), 0)) > 0)
    {
        if (got_exit_status)
            break;
        if (n >= 4)
        {
            fwrite(output_buffer, 1, n - 4, stdout);
            memcpy(exit_status_buffer, output_buffer + n - 4, 4);
            got_exit_status = 1;
            break;
        }
        else
        {
            fwrite(output_buffer, 1, n, stdout);
        }
    }

    if (!got_exit_status)
    {
        while (exit_status_index < 4)
        {
            ssize_t r = recv(sock, exit_status_buffer + exit_status_index, 4 - exit_status_index, 0);
            if (r <= 0)
            {
                fprintf(stderr, "Failed to receive exit status\n");
                close(sock);
                return 1;
            }
            exit_status_index += r;
        }
    }

    int exit_status_network;
    memcpy(&exit_status_network, exit_status_buffer, 4);
    int exit_status = ntohl(exit_status_network);
    printf("\nExit status: %d\n", exit_status);

    close(sock);
    return 0;
}

inline void exitter(const char *err)
{
    perror(err);
    exit(EXIT_FAILURE);
}
