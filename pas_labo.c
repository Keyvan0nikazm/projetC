#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "header.h"
#include "utils_v3.h"
#include "pascman.h"
#include "game.h"

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <port> <map_file> <joueur1_file> <joueur2_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    char *map_file = argv[2];
    char *joueur1_file = argv[3];
    char *joueur2_file = argv[4];

    printf("Lancement de l'environnement de test pas_labo...\n");

    // Create two pipes for communication between pas_labo and the client processes
    int pipe_client1[2];
    int pipe_client2[2];

    // Create the pipes
    if (pipe(pipe_client1) == -1 || pipe(pipe_client2) == -1) {
        perror("Erreur lors de la création des pipes");
        exit(EXIT_FAILURE);
    }

    // First, launch pas_server with the map as input
    pid_t server_pid = sfork();
    if (server_pid == 0) {
        // Child process for pas_server
        printf("Lancement du serveur pas_server...\n");

        // Close pipes we don't need in this process
        sclose(pipe_client1[0]);
        sclose(pipe_client1[1]);
        sclose(pipe_client2[0]);
        sclose(pipe_client2[1]);

        // Execute pas_server
        execl("./pas_server", "pas_server", NULL);

        // If execl fails
        perror("Erreur lors du lancement de pas_server");
        exit(EXIT_FAILURE);
    }

    // Wait a moment before launching clients
    usleep(200000); // 0.2 seconds

    // Launch first client
    pid_t client1_pid = sfork();
    if (client1_pid == 0) {
        // Child process for pas_client 1
        printf("Lancement du client 1 (pas_client avec option -test)...\n");

        // Redirect pipe to stdin
        dup2(pipe_client1[0], STDIN_FILENO);

        // Close unused pipe ends
        sclose(pipe_client1[1]);
        sclose(pipe_client2[0]);
        sclose(pipe_client2[1]);

        // Execute pas_client with -test option
        execl("./pas_client", "pas_client", "-test", NULL);

        // If execl fails
        perror("Erreur lors du lancement de pas_client 1");
        exit(EXIT_FAILURE);
    }

    // Wait before launching second client
    usleep(200000); // 0.2 seconds

    // Launch second client
    pid_t client2_pid = sfork();
    if (client2_pid == 0) {
        // Child process for pas_client 2
        printf("Lancement du client 2 (pas_client avec option -test)...\n");

        // Redirect pipe to stdin
        dup2(pipe_client2[0], STDIN_FILENO);

        // Close unused pipe ends
        sclose(pipe_client2[1]);
        sclose(pipe_client1[0]);
        sclose(pipe_client1[1]);

        // Execute pas_client with -test option
        execl("./pas_client", "pas_client", "-test", NULL);

        // If execl fails
        perror("Erreur lors du lancement de pas_client 2");
        exit(EXIT_FAILURE);
    }

    // Parent process continues here
    // Close read ends of pipes we don't use
    sclose(pipe_client1[0]);
    sclose(pipe_client2[0]);

    printf("Tous les processus ont été lancés avec succès.\n");

    // Open movement files for both players
    FILE *file1 = fopen(joueur1_file, "r");
    FILE *file2 = fopen(joueur2_file, "r");
    if (!file1 || !file2) {
        perror("Erreur lors de l'ouverture des fichiers de mouvements");
        exit(EXIT_FAILURE);
    }

    char move1, move2;
    while (1) {
        // Read a movement for player 1
        if (fscanf(file1, " %c", &move1) == 1) {
            nwrite(pipe_client1[1], &move1, sizeof(move1));
        }

        usleep(100000); // Wait 1/10th of a second

        // Read a movement for player 2
        if (fscanf(file2, " %c", &move2) == 1) {
            nwrite(pipe_client2[1], &move2, sizeof(move2));
        }

        usleep(100000); // Wait 1/10th of a second

        // Break if end of both files is reached
        if (feof(file1) && feof(file2)) {
            break;
        }
    }

    fclose(file1);
    fclose(file2);

    // Close write ends when done
    sclose(pipe_client1[1]);
    sclose(pipe_client2[1]);

    // Wait for 5 seconds to observe the game result
    sleep(5);

    // Wait for all child processes to finish
    int status;
    waitpid(client1_pid, &status, 0);
    waitpid(client2_pid, &status, 0);
    waitpid(server_pid, &status, 0);

    printf("Tous les processus ont terminé. Fin de pas_labo.\n");

    return 0;
}
