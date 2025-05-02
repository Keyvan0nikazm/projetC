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
        
        // Load the map file
        FileDescriptor map = sopen("./resources/map.txt", O_RDONLY, 0);
        
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
    
    // Here you can write commands to the pipes to control the clients
    // For example, simulating keyboard input or predefined movement patterns
    
    // Close write ends when done
    sclose(pipe_client1[1]);
    sclose(pipe_client2[1]);
    
    // Wait for all child processes to finish
    int status;
    waitpid(client1_pid, &status, 0);
    waitpid(client2_pid, &status, 0);
    waitpid(server_pid, &status, 0);
    
    printf("Tous les processus ont terminé. Fin de pas_labo.\n");
    
    return 0;
}
