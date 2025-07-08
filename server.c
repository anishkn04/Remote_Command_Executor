#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

const int PORT = 8080;

void exitter(const char *err);

int main(){
    signal(SIGPIPE, SIG_IGN);
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        exitter("setsockot failed");
    }
    if(server_fd == 0){
        exitter("Socket failed!");
    }
    
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0){
        exitter("Bind failed");
    }
    
    printf("\nServer running on port: %d\n", PORT);
    if (listen(server_fd, 3) < 0){
        exitter("Listen failed");
    }

    int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if(client_socket < 0) {
        exitter("Accept failed");
    }
    printf("Client connected!\n");


    char buffer[1024] = {0};
    int valread = recv(client_socket, buffer, sizeof(buffer), 0);
    if(valread < 0){
        exitter("Receive failed");
    }
    printf("Received Command: %s\n", buffer);
    
    int pipefd[2];
    if(pipe(pipefd) == -1){
        exitter("Pipe failed");
    }

    pid_t child_pid = fork();
    if(child_pid < 0){
        exitter("Fork failed - child");
    }

    if(child_pid == 0) {
        close(pipefd[0]);
        
        pid_t grandchild_pid = fork();
        if(grandchild_pid < 0){
            exitter("Fork failed - grandchild");
        }

        if (grandchild_pid == 0)
        {
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
            send(client_socket, output_buffer, nbytes, 0);
        }

        
        int status;
        waitpid(grandchild_pid, &status, 0);
        int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        exit_status = htonl(exit_status);
        send(client_socket, &exit_status, sizeof(exit_status), 0);
        
        shutdown(client_socket, SHUT_WR);
        close(client_socket);

        printf("Command executed, output sent, exiting child.\n");
        exit(0);
    }

    close(client_socket);
    close(pipefd[0]);
    close(pipefd[1]);
    
    waitpid(child_pid, NULL, 0);

    return 0;
}

inline void exitter(const char *err){
    perror(err);
    exit(EXIT_FAILURE);
}