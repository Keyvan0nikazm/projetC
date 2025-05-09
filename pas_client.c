#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ctype.h>

#include "header.h"
#include "game.h"
#include "pascman.h"
#include "utils_v3.h"

int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s <server_ip> <server_port> [-test]\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  char *server_ip = argv[1];
  int server_port = atoi(argv[2]);
  int test_mode = (argc == 4 && strcmp(argv[3], "-test") == 0);

  printf("Welcome to the game server registration program\n");

  // Create and connect the socket
  int sockfd = ssocket();
  sconnect(server_ip, server_port, sockfd);

  // Wait for the server response - expect a REGISTRATION message
  union Message reg_msg;
  ssize_t ret = sread(sockfd, &reg_msg, sizeof(union Message));
  if (ret > 0 && reg_msg.registration.msgt == REGISTRATION) {
    printf("Server registered client as player %d\n", reg_msg.registration.player);
    if (reg_msg.registration.player == 0) {
      printf("Registration denied: game is full\n");
      sclose(sockfd);
      return EXIT_FAILURE;
    }
  } else {
    printf("Error receiving server response or invalid message\n");
  }

  // Create a pipe for communication between parent and child processes
  int pipefd[2];
  spipe(pipefd);
  
  int childId = sfork();
  if (childId == 0) { // Child process (pas-cman-ipl)
    
    // Close the read end of the pipe in the child process
    sclose(pipefd[0]);
    
    // Redirect stdout to the pipe (write end)
    dup2(pipefd[1], STDOUT_FILENO);
    
    // Redirect the socket to stdin
    dup2(sockfd, STDIN_FILENO);

    // Close the original file descriptors
    sclose(pipefd[1]);
    sclose(sockfd);
    
    // Execute the graphical interface process
    printf("Launching the graphical interface pas-cman-ipl...\n");
    execl("./target/release/pas-cman-ipl", "pas-cman-ipl", NULL);

    // This code is only reached if execl fails
    perror("Error executing pas-cman-ipl");
    exit(EXIT_FAILURE);
  } else { // Parent process
    
    // Close the write end of the pipe in the parent process
    sclose(pipefd[1]);
    
    // Initialize the game state
    struct GameState state;
    // Initialize game state here if needed
    
    // Handle test mode in the parent process
    if (test_mode) {
      printf("Test mode enabled: reading moves from stdin\n");
      
      // Use proper message structure for Direction
      enum Direction dir;
      ssize_t taille;
      while ((taille = sread(STDIN_FILENO, &dir, sizeof(dir))) > 0) {
        // Create a proper movement message
        union Message move_msg;
        move_msg.movement.msgt = MOVEMENT;
        move_msg.movement.id = (reg_msg.registration.player == 1) ? PLAYER1_ID : PLAYER2_ID;
        // Direction is sent directly as we're in test mode
        nwrite(sockfd, &dir, sizeof(dir));
      }
    } else {
      // Buffer for reading from the pipe
      char command_buffer[1024];
      ssize_t bytes_read;

      // Main communication loop
      while ((bytes_read = sread(pipefd[0], command_buffer, 1024 - 1)) > 0) {
        command_buffer[bytes_read] = '\0';

        // Skip empty packages
        if (bytes_read == 0) {
          continue;
        }

        // Parse the command from the child
        if (bytes_read >= 1) {
          int direction_value = (unsigned char)command_buffer[0];
          
          // Ignore textual data (e.g., ASCII letters)
          if (isalpha(direction_value)) {
            continue;
          }

          // Check if the direction value is valid
          if (direction_value >= 0 && direction_value <= 3) {
            enum Direction dir = (enum Direction)direction_value;

            // Send the direction using the proper message format
            nwrite(sockfd, &dir, sizeof(dir));
          }
        }
      }
      
      // Close the read end of the pipe when done
      sclose(pipefd[0]);
    }

    // Wait for the child process to terminate
    int status;
    swaitpid(childId, &status, 0);
  }

  // Close the socket
  sclose(sockfd);
  return 0;
}