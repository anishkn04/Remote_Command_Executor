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

#define PORT 8080
#define MAX_CLIENTS 10
#define MAX_BLACKLIST_ENTRIES 100
#define MAX_COMMAND_LENGTH 256

char blacklist[MAX_BLACKLIST_ENTRIES][MAX_COMMAND_LENGTH];
int blacklist_count = 0;

void exitter(const char *err);
void *handle_client(void *arg);
void load_blacklist(const char *filename);
int is_command_allowed(const char *command);

typedef struct
{
    int client_socket;
    struct sockaddr_in client_addr;
    int client_id;
} client_info_t;

int main()
{
    signal(SIGPIPE, SIG_IGN);
    
    int server_fd;
    struct sockaddr_in address;
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) exitter("Socket failed");

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        exitter("setsockopt failed");

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        exitter("Bind failed");

    printf("Server running on port: %d\n", PORT);

    if (listen(server_fd, MAX_CLIENTS) < 0) exitter("Listen failed");

    load_blacklist("blacklist.txt");
    printf("Loaded %d blacklisted commands\n", blacklist_count);

    int client_counter = 0;

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

        client_info_t *client_info = malloc(sizeof(client_info_t));
        if (!client_info)
        {
            perror("Memory allocation failed");
            close(client_socket);
            continue;
        }

        client_info->client_socket = client_socket;
        client_info->client_addr = client_addr;
        client_info->client_id = client_counter;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, (void *)client_info) != 0)
        {
            perror("Thread creation failed");
            close(client_socket);
            free(client_info);
            continue;
        }

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

    char buffer[1024] = {0};
    int valread = recv(client_socket, buffer, sizeof(buffer), 0);
    if (valread < 0)
    {
        perror("Receive failed");
        goto cleanup;
    }

    printf("[Client %d] Command: '%s'\n", client_id, buffer);

    if (!is_command_allowed(buffer))
    {
        printf("[Client %d] Command is blacklisted: '%s'\n", client_id, buffer);
        const char *error_message = "Error: Command is not allowed\n";
        send(client_socket, error_message, strlen(error_message), 0);
        
        int exit_status_network = htonl(1);
        send(client_socket, &exit_status_network, sizeof(exit_status_network), 0);
        
        shutdown(client_socket, SHUT_WR);
        goto cleanup;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("Pipe failed");
        goto cleanup;
    }

    pid_t child_pid = fork();
    if (child_pid < 0)
    {
        perror("Fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        goto cleanup;
    }

    if (child_pid == 0)
    {
        pid_t grandchild_pid = fork();
        if (grandchild_pid < 0)
        {
            perror("Fork failed - grandchild");
            exit(1);
        }

        if (grandchild_pid == 0)
        {
            close(client_socket);
            close(pipefd[0]);
            
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            chdir("/tmp");
            execlp("/bin/sh", "sh", "-c", buffer, (char *)NULL);
            perror("Exec failed");
            exit(127);
        }

        close(pipefd[1]);

        char output_buffer[1024];
        ssize_t nbytes;

        while ((nbytes = read(pipefd[0], output_buffer, sizeof(output_buffer))) > 0)
        {
            if (send(client_socket, output_buffer, nbytes, 0) < 0) break;
        }

        close(pipefd[0]);

        int status;
        waitpid(grandchild_pid, &status, 0);

        int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        int exit_status_network = htonl(exit_status);
        send(client_socket, &exit_status_network, sizeof(exit_status_network), 0);

        shutdown(client_socket, SHUT_WR);
        close(client_socket);
        exit(0);
    }

    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(child_pid, NULL, 0);
    printf("[Client %d] Output sent\n", client_id);

cleanup:
    close(client_socket);
    free(client_info);
    return NULL;
}

void exitter(const char *err)
{
    perror(err);
    exit(EXIT_FAILURE);
}

void load_blacklist(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        printf("Warning: Could not open blacklist file '%s'. All commands will be allowed.\n", filename);
        blacklist_count = 0;
        return;
    }

    blacklist_count = 0;
    char line[MAX_COMMAND_LENGTH];
    
    while (fgets(line, sizeof(line), file) != NULL && blacklist_count < MAX_BLACKLIST_ENTRIES)
    {
        line[strcspn(line, "\n")] = 0;
        
        if (line[0] == '#' || line[0] == '\0') continue;
        
        strcpy(blacklist[blacklist_count], line);
        blacklist_count++;
    }

    fclose(file);
}

int is_command_allowed(const char *command)
{
    for (int i = 0; i < blacklist_count; i++)
    {
        if (strstr(command, blacklist[i]) != NULL)
        {
            return 0;
        }
    }
    return 1;
}