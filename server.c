#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

const int PORT = 8080;
const int MAX_CLIENTS = 10;

void exitter(const char *err);
void *handle_client(void *arg);

typedef struct
{
    int client_socket;
    struct sockaddr_in client_addr;
    int client_id;
} client_info_t;

int main()
{
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to prevent crashes on client disconnect

    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Setup address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        exitter("Socket failed");
    }

    // Reuse port
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        exitter("setsockopt failed");
    }

    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        exitter("Bind failed");
    }

    printf("Server running on port: %d\n", PORT);
    printf("Ready to accept up to %d concurrent clients\n", MAX_CLIENTS);

    // Listen
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        exitter("Listen failed");
    }

    int client_counter = 0;

    // Accept clients in a loop
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        client_counter++;
        printf("Client %d connected from %s:%d\n",
               client_counter, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Create client info structure
        client_info_t *client_info = malloc(sizeof(client_info_t));
        if (!client_info)
        {
            perror("Failed to allocate memory for client info");
            close(client_socket);
            continue;
        }

        client_info->client_socket = client_socket;
        client_info->client_addr = client_addr;
        client_info->client_id = client_counter;

        // Create thread to handle client
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, (void *)client_info) != 0)
        {
            perror("Failed to create thread");
            close(client_socket);
            free(client_info);
            continue;
        }

        // Detach thread so it can clean up automatically
        pthread_detach(thread);
    }

    close(server_fd);
    return 0;
}

void *handle_client(void *arg)
{
    client_info_t *client_info = (client_info_t *)arg;
    int client_socket = client_info->client_socket;
    int client_id = client_info->client_id;

    printf("[Client %d] Handler started\n", client_id);

    // Receive command
    char buffer[1024] = {0};
    int valread = recv(client_socket, buffer, sizeof(buffer), 0);
    if (valread < 0)
    {
        perror("Receive failed");
        goto cleanup;
    }

    printf("[Client %d] Received Command: '%s' (length: %zu)\n",
           client_id, buffer, strlen(buffer));

    // Setup pipe
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("Pipe failed");
        goto cleanup;
    }

    pid_t child_pid = fork();
    if (child_pid < 0)
    {
        perror("Fork failed - child");
        close(pipefd[0]);
        close(pipefd[1]);
        goto cleanup;
    }

    if (child_pid == 0)
    {
        // In child process
        pid_t grandchild_pid = fork();
        if (grandchild_pid < 0)
        {
            perror("Fork failed - grandchild");
            exit(1);
        }

        if (grandchild_pid == 0)
        {
            // In grandchild - execute the command
            close(client_socket);
            close(pipefd[0]); // Close read end in grandchild

            // Redirect stdout and stderr to pipe
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            chdir("/tmp");
            execlp("/bin/sh", "sh", "-c", buffer, (char *)NULL);

            // If exec fails
            perror("Exec failed");
            exit(127);
        }

        // Back in child - relay output from grandchild to client
        close(pipefd[1]); // Close write end in child

        char output_buffer[1024];
        ssize_t nbytes;
        int any_output = 0;

        // Read from pipe and send to client
        while ((nbytes = read(pipefd[0], output_buffer, sizeof(output_buffer))) > 0)
        {
            if (send(client_socket, output_buffer, nbytes, 0) < 0)
            {
                break;
            }
            any_output = 1;
        }

        if (!any_output)
        {
            printf("[Client %d] No output read from pipe.\n", client_id);
        }

        close(pipefd[0]);

        // Wait for grandchild to finish
        int status;
        waitpid(grandchild_pid, &status, 0);

        int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

        // Send exit status to client
        int exit_status_network = htonl(exit_status);
        send(client_socket, &exit_status_network, sizeof(exit_status_network), 0);

        shutdown(client_socket, SHUT_WR);
        close(client_socket);

        printf("[Client %d] Command executed, output sent, exiting child.\n", client_id);
        exit(0);
    }

    // Parent process (in thread)
    close(pipefd[0]);
    close(pipefd[1]);

    waitpid(child_pid, NULL, 0);
    printf("[Client %d] Handler completed\n", client_id);

cleanup:
    close(client_socket);
    free(client_info);
    printf("[Client %d] Connection closed\n", client_id);
    return NULL;
}

void exitter(const char *err)
{
    perror(err);
    exit(EXIT_FAILURE);
}