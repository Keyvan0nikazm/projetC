#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include "header.h"
#include "game.h"
#include "utils_v3.h"

// Reference the existing 'end' variable instead of redefining it
extern volatile int end;

// Add a signal handler specifically for SIGINT
void sigintHandler(int sig) {
  printf("\nReceived SIGINT. Shutting down server...\n");
  end = 1;
}

int main(int argc, char **argv)
{

  if (argc != 3 && argc != 4) {
    fprintf(stderr, "Usage: %s <port> <map_file> [-test]\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  int port = atoi(argv[1]);
  char *map_file = argv[2];
  int test_mode = (argc == 4 && strcmp(argv[3], "-test") == 0); // Check if test mode is enabled

  if (test_mode) {
    printf("SERVER Test mode enabled. The server will not restart registration after a game.\n");
  } else {
    printf("SERVER Running in normal mode.\n");
  }

  int keep_running = 1;  // Flag to control the server main loop
  
  // Set up signal handling once for the entire program
  sigset_t set;
  ssigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  
  // Set up signal handlers before blocking signals
  ssigaction(SIGTERM, endServerHandler);
  ssigaction(SIGINT, sigintHandler); // Use the specific SIGINT handler
  ssigaction(SIGALRM, alarmHandler); // Handle SIGALRM for the timer

  // Block signals during critical initialization
  ssigprocmask(SIG_BLOCK, &set, NULL);

  // Initialize the server socket once
  int sockfd = initSocketServer(port);
  printf("Server running on port: %i\n", port);

  // Main server loop - continues until the server is explicitly terminated
  while (keep_running && !end) {

    // Unblock signals to allow handling of SIGINT
    ssigprocmask(SIG_UNBLOCK, &set, NULL);
    
    Player tabPlayers[MAX_PLAYERS];
    int nbPlayers = 0;
    registrationTimeout = 0;  // Reset timeout flag for the new registration phase
    
    printf("Starting the registration phase...\n");

    // Registration phase loop
    while (!end && !registrationTimeout && nbPlayers < MAX_PLAYERS) {
      int newsockfd;
      while ((newsockfd = accept(sockfd, NULL, NULL)) < 0) {
        if (errno == EINTR) {
          if (end) {
            keep_running = 0;
            break;
          }
          if (registrationTimeout) {
            break;
          }
          continue; // Retry accept() if interrupted
        }
        perror("ERROR accept");
        exit(EXIT_FAILURE);
      }

      // Handle registration timeout
      if (registrationTimeout && nbPlayers == 1) {
        const char *timeout_msg = "Registration timeout with only one player. Disconnecting the player.\n";
        if (tabPlayers[0].sockfd >= 0) {
          write(tabPlayers[0].sockfd, timeout_msg, strlen(timeout_msg));
          sclose(tabPlayers[0].sockfd);
          tabPlayers[0].sockfd = -1;
        }
        nbPlayers = 0;
        registrationTimeout = 0;
        break;
      }

      // Check if termination signal was received
      if (end) {
        close(newsockfd);
        keep_running = 0;
        break;
      }

      if (nbPlayers < MAX_PLAYERS) {
        tabPlayers[nbPlayers].sockfd = newsockfd;
        nbPlayers++;

        char message[50];
        snprintf(message, sizeof(message), "Client %d registered for a game\n", nbPlayers);
        nwrite(newsockfd, message, strlen(message));
        if (nbPlayers == 1) {
          printf("First client connected, starting the 30-second timer...\n");
          startRegistrationTimer();
          ssigprocmask(SIG_BLOCK, &set, NULL);
        } else if (nbPlayers == MAX_PLAYERS) {
          printf("Second client connected, stopping the timer.\n");
          alarm(0);
          break;
        }
      } else {
        const char *message = "Game full, registration denied\n";
        nwrite(newsockfd, message, strlen(message));
        sclose(newsockfd);
      }
    }

    // Skip the game if no players registered
    if (nbPlayers == 0 || end) {
      continue;
    }

    printf("Registration phase complete. Starting the game with %d player(s).\n", nbPlayers);

    // Shared memory and semaphore initialization
    int shm_id = sshmget(SHM_KEY, 2 * sizeof(pid_t), IPC_CREAT | PERM);
    struct GameState *shared_state = (struct GameState *)shmat(shm_id, NULL, 0);
    sem_create(SEM_KEY, 1, PERM, 1);
    int sem_id = sem_get(SEM_KEY, 1);

    struct GameState gameState;

    int pipefd[2];
    spipe(pipefd);

    // Create a client-handler for each connected client
    int clientHandlers[MAX_PLAYERS];
    for (int i = 0; i < nbPlayers; i++) {
      int childId = sfork();
      if (childId == 0) {
        // CLIENT-HANDLER for player i
        sclose(pipefd[0]);

        enum Direction dir;
        ssize_t move_bytes;
        while ((move_bytes = sread(tabPlayers[i].sockfd, &dir, sizeof(dir))) > 0) {
          sem_down(sem_id, 0);  // Lock shared memory

          // Process the move with process_user_command
          int player_id = i + 1;  // Player ID (1 or 2)
          enum Item player = (player_id == 1) ? PLAYER1 : PLAYER2;

          // Update the game state and send necessary messages
          bool game_over = process_user_command(shared_state, player, dir, pipefd[1]);

          sem_up(sem_id, 0);  // Release the lock on shared memory
        }

        // End client connection
        sclose(tabPlayers[i].sockfd);
        sclose(pipefd[1]);
        shmdt(shared_state);
        exit(0);
      } else {
        clientHandlers[i] = childId;
      }
    }

    // Broadcaster process
    int childId2 = sfork();
    if (childId2 == 0) {
      sclose(pipefd[1]);

      union Message buffer;
      ssize_t bytes_read;

      // Send registration message to each client
      for (int i = 0; i < nbPlayers; i++) {
        send_registered(i + 1, tabPlayers[i].sockfd);
      }

      while ((bytes_read = sread(pipefd[0], &buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < nbPlayers; i++) {
          nwrite(tabPlayers[i].sockfd, &buffer, bytes_read);
        }
      }

      sclose(pipefd[0]);
      exit(0);
    } else {
      sclose(pipefd[0]);

      // Load the map
      FileDescriptor fdmap = sopen(map_file, O_RDONLY, 0);
      load_map(fdmap, pipefd[1], &gameState);
      sclose(fdmap);

      // Synchronize shared memory with the initial game state
      memcpy(shared_state, &gameState, sizeof(struct GameState));
      shmdt(shared_state);

      // Wait for client-handlers to finish
      int status;
      for (int i = 0; i < nbPlayers; i++) {
        swaitpid(clientHandlers[i], &status, 0);
      }

      sclose(pipefd[1]);
      swaitpid(childId2, &status, 0);

      // Clean up game resources
      if (shm_id >= 0) {
        sshmdelete(shm_id);
      }

      if (sem_id >= 0) {
        sem_delete(sem_id);
      }

      // Close client sockets
      for (int i = 0; i < nbPlayers; i++) {
        if (tabPlayers[i].sockfd >= 0) {
          sclose(tabPlayers[i].sockfd);
        }
      }
    }

    printf("Game finished.\n");

    // Exit the main loop if in test mode
    if (test_mode) {
      printf("Test mode active: exiting after the game.\n");
      break;
    }

    printf("Preparing for a new registration phase.\n");
  }

  // Cleanup before exiting
  if (sockfd >= 0) {
    sclose(sockfd);
  }

  printf("Server stopped.\n");
  return 0;
}