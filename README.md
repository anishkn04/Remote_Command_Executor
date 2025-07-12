# Remote Command Executor

A simple client-server application in C for executing shell commands remotely over TCP. The server executes commands sent by the client, streams back the output and exit status. **For educational use only. Not secure for production!**

---

## Table of Contents
- [Quick Start](#quick-start)
- [Project Group](#project-group)
- [Introduction](#1-introduction)
- [Features](#2-features)
- [Limitations and Security Disclaimers](#3-limitations-and-critical-security-disclaimers)
- [Technical Design Highlights](#4-technical-design-highlights)
- [Compilation](#5-compilation)

---

## Quick Start

### 1. Build the Programs

```bash
make
```
This will create `compiled_server` and `compiled_client` executables.

### 2. Run the Server

```bash
./compiled_server
```

### 3. Run the Client (in another terminal)

```bash
./compiled_client
```

- Enter a shell command when prompted. The output and exit status will be displayed.

---

## Project Group:
* Anish Kumar Neupane, 221608
* Rushab Risal, 221637
* Prayojan Puri, 221632

## Date: July 8, 2025

---

## 1. Introduction

This project implements a basic Remote Command Executor using TCP sockets in C on a Linux environment. It serves as a practical demonstration of advanced network programming concepts including:
* Reliable client-server communication.
* Concurrency using `fork()` for handling multiple clients.
* Inter-process communication (IPC) via `pipe()`.
* Process management using `fork()` and `exec()`.
* Basic command validation.

The application consists of a server that can receive and execute shell commands from a client. It captures the standard output and standard error of the executed command and returns them, along with the command's exit status, back to the client.

---

## 2. Features

* **Client-Server Architecture:** A dedicated server listens for connections, and clients can connect to issue commands.
* **TCP-based Communication:** All data transfer (commands, output, and exit status) occurs over Transmission Control Protocol (TCP) for reliability.
* **Command Request Mechanism:** The client sends a string representing a shell command to the server.
* **Remote Command Execution:** The server receives the command string and executes it using a local shell (`/bin/sh`).
* **Output Redirection:** The standard output (`stdout`) and standard error (`stderr`) of the executed command on the server are captured.
* **Output Transmission:** The captured output is streamed back to the client. The client dynamically resizes its buffer to accommodate varying output lengths.
* **Concurrency (Fork-based):** The server handles multiple client connections concurrently by `fork()`ing a new child process for each accepted client. This child process then manages the command execution and communication for that specific client. This child process further `fork()`s a grandchild to execute the command itself, ensuring proper output capture.
* **Basic Error Handling:** Robust error checking is implemented for socket operations, process creation, and command execution failures using `perror()` and `exit()`.
* **Command Blacklisting:** The server implements a basic blacklist mechanism to disallow specific dangerous commands (e.g., `rm -rf`, `reboot`) before execution, sending an error message and specific exit status if a blacklisted command is received.
* **Sending Command Exit Status:** After executing a command, the server captures its exit status (0 for success, non-zero for failure) and sends this status back to the client, providing crucial feedback on the command's outcome.
* **Zombie Process Handling:** The server uses a `SIGCHLD` signal handler to automatically reap exited child processes, preventing zombie accumulation.
* **Port Reuse (`SO_REUSEADDR`):** Allows the server to be restarted quickly after termination without waiting for the port to clear.
* **`SIGPIPE` Handling:** `SIGPIPE` is ignored to prevent server crashes if a client disconnects unexpectedly during data transmission.

---

## 3. Limitations and **CRITICAL SECURITY DISCLAIMERS**

**THIS PROJECT IS FOR EDUCATIONAL PURPOSES ONLY AND IS NOT SECURE FOR PRODUCTION USE.**

* **No Strong Authentication/Authorization:** Any client that can connect to the server's port can attempt to execute commands. There is no user login or permission system.
* **No Encryption:** All communication (commands, output, exit status) is transmitted in plain text over the network. This makes it vulnerable to eavesdropping.
* **Basic Command Validation (Blacklisting):** The implemented blacklist is simplistic and easily bypassed by a knowledgeable attacker. It relies on `strstr()` which means a command like `r m -rf` or `R M -RF` or using shell features like `$(echo rm) -rf` could bypass it. It is *not* a production-ready security solution. **A robust remote command executor requires sophisticated parsing, strict whitelisting, and possibly sandboxing (e.g., `chroot` jails, `namespaces`, `seccomp`).**
* **No Interactive Shell Features:** This application does not provide a persistent interactive shell (like SSH). Each command is executed as a separate request.
* **Limited File Transfer:** Focus is on command output, not large binary file transfers.
* **Platform Specific:** Strictly designed for Linux/Unix-like environments, leveraging Unix Sockets API.

---

## 4. Technical Design Highlights

### Server Architecture:
The server uses a **fork-based concurrency model**.
1.  The `main` server process `fork()`s a new child process for each incoming client connection. This allows the server to remain responsive and continue accepting new clients simultaneously.
2.  Each child process (acting as a client handler) then receives the client's command.
3.  Before execution, the client handler performs **command validation** against a predefined blacklist. If the command is blacklisted, an error is sent to the client, and the child process exits.
4.  If the command is valid, the client handler `fork()`s *another* process (a "grandchild").
5.  This grandchild process:
    * Closes the read end of a `pipe()` and the client socket (which it doesn't need).
    * Uses `dup2()` to redirect its `STDOUT_FILENO` and `STDERR_FILENO` to the write end of the `pipe()`.
    * Executes the client's command using `execlp("/bin/sh", "sh", "-c", command, (char *)NULL)`.
6.  The client handler (the child process, parent of the grandchild):
    * Closes the write end of the `pipe()` (as it only needs to read).
    * Reads the command's output from the read end of the `pipe()`.
    * Streams this output back to the client via the connected socket.
    * Uses `shutdown(client_socket, SHUT_WR)` to signal the end of output to the client.
    * `waitpid()`s for the grandchild to complete to retrieve its exit status.
    * Sends the command's exit status (as an integer) to the client.
    * Closes the client socket and `exit()`s.
7.  The main server process uses a `SIGCHLD` signal handler to `waitpid()` for its child processes (client handlers) in a non-blocking manner, preventing zombie processes.

### Communication Protocol:
1.  **Client Request:** Client sends the shell command as a null-terminated string.
2.  **Server Response (Output):** Server streams captured `stdout` and `stderr` as raw bytes.
3.  **End of Output:** Server signals end of output by `shutdown()`ing the write half of the socket (`SHUT_WR`).
4.  **Exit Status:** Immediately after output, server sends a 4-byte integer (network byte order) representing the command's exit status.

### Inter-Process Communication (IPC) & Process Management:
* `fork()`: Used extensively for concurrency (main server -> client handler) and command execution (client handler -> command executor).
* `pipe()`: Used to capture `stdout` and `stderr` from the command execution process.
* `dup2()`: Redirects standard file descriptors (`stdout`, `stderr`) to the pipe.
* `execlp()`: Replaces a process image with `/bin/sh` to execute the command.
* `waitpid()`: Used by the parent process to wait for and collect the exit status of its children, preventing zombies.
* `signal()` & `sigaction()`: Used to set up a `SIGCHLD` handler for automatic zombie reaping and `SIGPIPE` handling to prevent crashes.

### Socket API (Unix Sockets):
* `socket()`, `bind()`, `listen()`, `accept()`, `connect()`: For TCP connection management.
* `send()`, `recv()`: For transmitting data.
* `close()`: To close file descriptors.
* `shutdown()`: To gracefully signal end of data transmission.
* `htons()`, `ntohs()`, `inet_pton()`, `inet_ntoa()`, `htonl()`, `ntohl()`: For network address and byte order conversions.
* `setsockopt(SO_REUSEADDR)`: For convenient server restarts during development.

---

## 5. Compilation

To compile the server and client applications:

```bash
# Compile the server
gcc server.c -o server

# Compile the client
gcc client.c -o client
```