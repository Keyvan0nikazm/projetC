#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "header.h"
#include "game.h"
#include "pascman.h"
#include "utils_v3.h"

int main(int argc, char *argv[]) {
  printf("Bienvenue dans le programme d'inscription au serveur de jeu\n");

  int sockfd = ssocket();
  sconnect(SERVER_IP, SERVER_PORT, sockfd);

  /* wait server response */
  char buffer[50];
  ssize_t ret = sread(sockfd, buffer, sizeof(buffer) - 1);
  if (ret > 0)
  {
    buffer[ret] = '\0';
    printf("Réponse du serveur : %s", buffer);
  }
  else
  {
    printf("Erreur lors de la réception de la réponse du serveur\n");
  }

  // Create pipe for child-parent communication
  int pipefd[2];
  spipe(pipefd);
  
  int childId = sfork();
  if (childId == 0) { // Child process (pas-cman-ipl)
    printf("Je suis le processus PAC-MAN-IPL (pid: %d)\n", getpid());
    
    // Close read end of pipe in child
    sclose(pipefd[0]);
    
    // Redirect stdout to the pipe (write end)
    dup2(pipefd[1], STDOUT_FILENO);
    
    // Redirect socket to stdin
    dup2(sockfd, STDIN_FILENO);

    // Close the original file descriptors
    sclose(pipefd[1]);
    sclose(sockfd);
    
    // Execute pas-cman-ipl
    printf("Lancement de l'interface graphique pas-cman-ipl...\n");
    execl("./target/release/pas-cman-ipl", "pas-cman-ipl", NULL);

    // This code is only reached if execl fails
    perror("Error executing pas-cman-ipl");
    exit(EXIT_FAILURE);
  } else { // Parent process
    printf("Je suis le processus parent (pid: %d)\n", getpid());
    
    // Close write end of pipe in parent
    sclose(pipefd[1]);
    
    // Game state initialization
    struct GameState state;
    // Initialize game state here if needed
    
    // Buffer for reading from pipe
    char command_buffer[1024];
    ssize_t bytes_read;

    // Main communication loop
    while ((bytes_read = sread(pipefd[0], command_buffer, 1024 - 1)) > 0) {
      command_buffer[bytes_read] = '\0';
      
      // Debugging: Print raw received data with visible representation of non-printable chars
      printf("Raw data received (%ld bytes): ", bytes_read);
      for (int i = 0; i < bytes_read; i++) {
        if (command_buffer[i] >= 32 && command_buffer[i] <= 126)
          printf("%c", command_buffer[i]);
        else
          printf("[%02X]", (unsigned char)command_buffer[i]);
      }
      printf("\n");
      
      // Skip empty packages
      if (bytes_read == 0) {
        continue;
      }
      
      // Parse the command from the child
      enum Item player = PLAYER1; // Default player
      enum Direction dir = DOWN;  // Default direction
      
      // For binary data, interpret the first byte as the direction value
      if (bytes_read >= 1) {
        int direction_value = (unsigned char)command_buffer[0];
        
        // Check if direction value is valid
        if (direction_value >= 0 && direction_value <= 3) {
          dir = (enum Direction)direction_value;
          printf("Received direction (binary): %d\n", dir);
          
          // Process the command
          // Envoyer la direction au serveur via le socket
          nwrite(sockfd, &dir, sizeof(dir));

        } else {
          printf("Invalid direction value: %d\n", direction_value);
        }
      }
    }
    
    // Close the read end when done
    sclose(pipefd[0]);

    // Wait for child process to terminate
    int status;
    swaitpid(childId, &status, 0);
  }

  sclose(sockfd);
  return 0;
}