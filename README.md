# NP Project

## Introduction

This project implements a client-server network application using C. It demonstrates basic networking concepts such as socket programming, message exchange, and concurrent client handling.<br>
The server also supports command blacklisting feature.
You can add any commands you want to blacklist in the `blacklist.txt` file

## How to Compile

1. Open a terminal and navigate to the project directory
2. Run the following command to compile the project:
```sh
make
```

## How to Run

After successful compilation, execute the server program with:
```sh
./compiled_server
```
Or,
```bash
make run-server
```
And, for the client program, execute the following command:
```sh
./compiled_client
```
Or,
```bash
make run-client
```

## Cleaning Up

To remove compiled files, run:
```sh
make clean
```