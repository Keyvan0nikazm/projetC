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
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <port> <map_file>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  int port = atoi(argv[1]);
  char *map_file = argv[2];

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
  printf("Le serveur tourne sur le port : %i \n", port);

  
  // Main server loop - continues until server is explicitly terminated
  while (keep_running && !end) {

    // Unblock signals to allow handling of SIGINT
    ssigprocmask(SIG_UNBLOCK, &set, NULL);
    
    Player tabPlayers[MAX_PLAYERS];
    int nbPlayers = 0;
    registrationTimeout = 0;  // Reset timeout flag for new registration phase
    
    printf("Démarrage de la phase d'inscription...\n");

    // Registration phase loop
    while (!end && !registrationTimeout && nbPlayers < MAX_PLAYERS)
    {
      int newsockfd;
      while ((newsockfd = accept(sockfd, NULL, NULL)) < 0)
      {
        if (errno == EINTR)
        {
          if (end) {
            printf("Termination signal received. Shutting down...\n");
            keep_running = 0;
            break;
          }
          
          if (registrationTimeout)
          {
            // Just break out of the accept loop, we'll handle the disconnect outside
            break;
          }
          continue; // Retry accept() if interrupted
        }
        perror("ERROR accept");
        exit(EXIT_FAILURE);
      }
      
      // Handle registration timeout outside the accept loop
      if (registrationTimeout && nbPlayers == 1) {
        printf("Timeout d'inscription avec un seul joueur. Déconnexion du joueur.\n");
        const char *timeout_msg = "Temps d'inscription écoulé sans deuxième joueur. Déconnexion.\n";
        if (tabPlayers[0].sockfd >= 0) {
          if (write(tabPlayers[0].sockfd, timeout_msg, strlen(timeout_msg)) < 0) {
            perror("Write error on timeout notification");
          }
          sclose(tabPlayers[0].sockfd);
          tabPlayers[0].sockfd = -1;
        }
        nbPlayers = 0;
        registrationTimeout = 0;
        break; // Exit the registration loop to restart
      }

      // Check if termination signal was received during accept
      if (end) {
        close(newsockfd);
        keep_running = 0;
        break;
      }

      if (nbPlayers < MAX_PLAYERS)
      {
        tabPlayers[nbPlayers].sockfd = newsockfd;
        nbPlayers++;

        char message[50];
        snprintf(message, sizeof(message), "Client %d inscrit à une partie\n", nbPlayers);
        nwrite(newsockfd, message, strlen(message));
        printf("%s", message);
        if (nbPlayers == 1)
        {
          printf("Premier client connecté, démarrage du chrono de 30 secondes...\n");
          startRegistrationTimer();

          ssigprocmask(SIG_BLOCK, &set, NULL);

        }
        else if (nbPlayers == MAX_PLAYERS)
        {
          printf("Deuxième client connecté, arrêt du chrono.\n");
          alarm(0);
          break;
        }
      }
      else
      {
        const char *message = "Partie complète, inscription refusée\n";
        nwrite(newsockfd, message, strlen(message));
        printf("%s", message);
        sclose(newsockfd);
      }
    }
    
    // Skip game if no players registered
    if (nbPlayers == 0 || end) {
      printf("Aucun joueur inscrit ou serveur en cours d'arrêt.\n");
      continue;
    }

    printf("Phase d'inscription terminée. Démarrage du jeu avec %d joueur(s).\n", nbPlayers);

    // GET SHARED MEMORY et initialisation mémoire partagée et sémaphore (parent)
    int shm_id = sshmget(SHM_KEY, 2 * sizeof(pid_t), IPC_CREAT | PERM);
    struct GameState *shared_state = (struct GameState *)shmat(shm_id, NULL, 0);
    sem_create(SEM_KEY, 1, PERM, 1);
    int sem_id = sem_get(SEM_KEY, 1);

    struct GameState gameState;

    int pipefd[2];
    int ret = spipe(pipefd);

    // Créer un client-handler par client connecté
    int clientHandlers[MAX_PLAYERS];
    for (int i = 0; i < nbPlayers; i++) {
        int childId = sfork();
        if (childId == 0) {
            // CLIENT-HANDLER pour le joueur i
            ret = sclose(pipefd[0]);

            char move_buffer[TAILLE];
            ssize_t move_bytes;
            while ((move_bytes = sread(tabPlayers[i].sockfd, move_buffer, sizeof(move_buffer))) > 0) {

                sem_down(sem_id, 0);  // Lock shared memory

                // Traitement du mouvement avec process_user_command
                int player_id = i + 1;  // Player ID (1 or 2)
                enum Direction dir = (enum Direction)move_buffer[0];
                enum Item player = (player_id == 1) ? PLAYER1 : PLAYER2;

                // Met à jour l'état du jeu et envoie les messages nécessaires
                bool game_over = process_user_command(shared_state, player, dir, pipefd[1]);

                sem_up(sem_id, 0);  // Release the lock on shared memory
            }
            if (move_bytes == 0) {
            } else if (move_bytes < 0) {
                perror("Erreur de lecture sur la socket client");
            }

            // Fin de connexion client
            sclose(tabPlayers[i].sockfd);
            ret = sclose(pipefd[1]);
            shmdt(shared_state);
            exit(0);
        } else {
            clientHandlers[i] = childId;
        }
    }

    // Broadcaster
    int childId2 = sfork();
    if (childId2 == 0) {
        // DEUXIÈME ENFANT (broadcaster)
        ret = sclose(pipefd[1]);

        char buffer[TAILLE];
        ssize_t bytes_read;
        
        // First send registration message to each client
        for (int i = 0; i < nbPlayers; i++) {
            send_registered(i+1, tabPlayers[i].sockfd); // Send player ID (1 or 2)
        }

        while((bytes_read = sread(pipefd[0], buffer, sizeof(buffer))) > 0) {
            for (int i = 0; i < nbPlayers; i++){
                nwrite(tabPlayers[i].sockfd, buffer, bytes_read);
            }
        }

        sclose(pipefd[0]);
        exit(0);
    } else {
        // PARENT
        ret = sclose(pipefd[0]); // Ferme l'extrémité de lecture

        // Chargement de la carte
        FileDescriptor fdmap = sopen(map_file, O_RDONLY, 0);
        load_map(fdmap, pipefd[1], &gameState);
        sclose(fdmap);

        // Synchroniser la mémoire partagée avec l'état initial du jeu
        memcpy(shared_state, &gameState, sizeof(struct GameState));
        shmdt(shared_state);
        
        // Attendre que les client-handlers terminent
        int status;
        printf("Attente de la fin de la partie...\n");
        for (int i = 0; i < nbPlayers; i++) {
            swaitpid(clientHandlers[i], &status, 0);
            printf("Client-handler %d terminé\n", i+1);
        }
        
        ret = sclose(pipefd[1]);
        swaitpid(childId2, &status, 0);
        printf("Broadcaster terminé\n");
        
        // Nettoyage des ressources de la partie
        if (shm_id >= 0) {
            sshmdelete(shm_id);
        }
        
        if (sem_id >= 0) {
            sem_delete(sem_id);
        }
        
        // Fermer les sockets des clients
        for (int i = 0; i < nbPlayers; i++) {
            if (tabPlayers[i].sockfd >= 0) {
                sclose(tabPlayers[i].sockfd);
            }
        }
        
        printf("Partie terminée. Préparation d'une nouvelle phase d'inscription.\n");
    }
  }
  
  // Cleanup before exiting
  if (sockfd >= 0) {
    sclose(sockfd);
  }
  
  printf("Le serveur est arrêté.\n");
  return 0;
}