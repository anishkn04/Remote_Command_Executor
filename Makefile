CC = gcc
CFLAGS = 

all: server client

server: server.c
	$(CC) $(CFLAGS) -o compiled_server server.c -lpthread

client: client.c
	$(CC) $(CFLAGS) -o compiled_client client.c

clean:
	rm -f server client
