CC=gcc

CFLAGS=-std=c17 -pedantic -Wall -Wvla -Werror  -Wno-unused-variable -Wno-unused-but-set-variable -D_DEFAULT_SOURCE

all: exemple pas_server pas_client

exemple: exemple.o game.o utils_v3.o
	$(CC) $(CFLAGS) -o exemple exemple.o game.o utils_v3.o

pas_server : pas_server.o game.o utils_v3.o server_utils.o
	$(CC) $(CFLAGS) -o pas_server pas_server.o game.o utils_v3.o server_utils.o

pas_client : pas_client.o game.o utils_v3.o server_utils.o
	$(CC) $(CFLAGS) -o pas_client pas_client.o game.o utils_v3.o server_utils.o

exemple.o: exemple.c
	$(CC) $(CFLAGS) -c exemple.c

server_utils.o: server_utils.c utils_v3.c
	$(CC) $(CFLAGS) -c server_utils.c

pas_server.o : pas_server.c utils_v3.c
	$(CC) $(CFLAGS) -c pas_server.c

pas_client.o : pas_client.c utils_v3.c
	$(CC) $(CFLAGS) -c pas_client.c

game.o: game.h game.c
	$(CC) $(CFLAGS) -c game.c $(INCLUDES)

utils_v3.o: utils_v3.h utils_v3.c
	$(CC) $(CFLAGS) -c utils_v3.c $(INCLUDES)

clean: 
	rm -rf *.o

mrpropre: clean
	rm -rf exemple