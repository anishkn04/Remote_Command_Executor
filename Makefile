CC = gcc

all: server client

server: server.c
	$(CC) -o compiled_server server.c -lpthread

client: client.c
	$(CC) -o compiled_client client.c

run-server:
	./compiled_server

run-client:
	./compiled_client

clean:
	rm -f compiled_server compiled_client
