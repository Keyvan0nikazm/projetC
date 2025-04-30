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

#define MAX_PLAYERS 2
#define BACKLOG 5
#define TAILLE 1024

typedef struct Player
{
  char pseudo[MAX_PSEUDO];
  int sockfd;
} Player;

volatile sig_atomic_t end = 0;
volatile sig_atomic_t registrationTimeout = 0; // Flag for registration timeout

void endServerHandler(int sig)
{
  end = 1;
}

void alarmHandler(int sig)
{
  registrationTimeout = 1; // Set timeout flag
  printf("Temps d'inscription écoulé. Fermeture du serveur...\n");
}

void startRegistrationTimer()
{
  alarm(30); // Set a 30-second timer
}

void terminate(Player *tabPlayers, int nbPlayers)
{
  printf("\nJoueurs inscrits : \n");
  for (int i = 0; i < nbPlayers; i++)
  {
    printf("  - Client %d inscrit\n", i + 1);
    const char *message = "Temps d'inscription écoulé. Fermeture du serveur.\n";
    nwrite(tabPlayers[i].sockfd, message, strlen(message));
    // Ne fermez pas les sockets ici si vous voulez continuer à utiliser les connexions
    // sclose(tabPlayers[i].sockfd);
  }
  
  // Au lieu de terminer le programme, continuez avec un jeu à un seul joueur
  // Ne faites pas exit(0) ici
  end = 1; // Signaler la fin de la phase d'inscription
}

/**
 * PRE:  serverPort: a valid port number
 * POST: on success, binds a socket to 0.0.0.0:serverPort and listens to it ;
 *       on failure, displays error cause and quits the program
 * RES: return socket file descriptor
 */
int initSocketServer(int serverPort)
{
  struct sockaddr_in addr;

  int sockfd = ssocket();
  sbind(SERVER_PORT, sockfd);
  slisten(sockfd, BACKLOG);

  return sockfd;
}

int main(int argc, char **argv)
{
  Player tabPlayers[MAX_PLAYERS];
  int nbPlayers = 0;

  sigset_t set;
  ssigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  ssigprocmask(SIG_BLOCK, &set, NULL);

  ssigaction(SIGTERM, endServerHandler);
  ssigaction(SIGINT, endServerHandler);
  ssigaction(SIGALRM, alarmHandler); // Handle SIGALRM for the timer

  int sockfd = initSocketServer(SERVER_PORT);
  printf("Le serveur tourne sur le port : %i \n", SERVER_PORT);
  ssigprocmask(SIG_UNBLOCK, &set, NULL);

  while (!end)
  {
    int newsockfd;
    while ((newsockfd = accept(sockfd, NULL, NULL)) < 0)
    {
      if (errno == EINTR)
      {
        if (registrationTimeout)
        {
          terminate(tabPlayers, nbPlayers); // Ne termine plus le programme, signale juste la fin
          break; // Sortez de la boucle pour continuer avec le programme
        }
        continue; // Retry accept() if interrupted
      }
      perror("ERROR accept");
      exit(EXIT_FAILURE);
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
        startRegistrationTimer(); // Start the 30-second timer
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

    if (registrationTimeout && nbPlayers < MAX_PLAYERS)
    {
      terminate(tabPlayers, nbPlayers); // Ne termine plus le programme, signale juste la fin
      break; // Sortez de la boucle pour continuer avec le programme
    }
  }
  
  printf("Phase d'inscription terminée. Démarrage du jeu avec %d joueur(s).\n", nbPlayers);

  // GET SHARED MEMORY
  int shm_id = sshmget(SHM_KEY,2 * sizeof(pid_t), IPC_CREAT | PERM);
  int *z = sshmat(shm_id);

  // Initialisation mémoire partagée et sémaphore (parent)
  int shm_id2 = sshmget(SHM_KEY, 2 * sizeof(pid_t), 0);
  struct GameState *shared_state = (struct GameState *)shmat(shm_id2, NULL, 0);
  int sem_id = sem_get(SEM_KEY, 1);

  sem_create(SEM_KEY, 1, PERM, 1);

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

          // Initialisation mémoire partagée et sémaphore (client-handler)
          int shm_id2 = sshmget(SHM_KEY, 2 * sizeof(pid_t), 0);
          struct GameState *shared_state = (struct GameState *)shmat(shm_id2, NULL, 0);
          int sem_id = sem_get(SEM_KEY, 1);

          // Boucle de réception des mouvements du client
          // Synchronisation mémoire partagée + sémaphore
          // Attach shared memory and get semaphore
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
      ret = sclose(sockfd);
      exit(0);
  } else {
      // PARENT
      ret = sclose(pipefd[0]); // Ferme l'extrémité de lecture

      // Chargement de la carte
      FileDescriptor fdmap = sopen("./resources/map.txt", O_RDONLY, 0);
      load_map(fdmap, pipefd[1], &gameState);
      sclose(fdmap);

      // Synchroniser la mémoire partagée avec l'état initial du jeu
      memcpy(shared_state, &gameState, sizeof(struct GameState));
      shmdt(shared_state);
      
      // Attendre que les client-handlers terminent
      int status;
      for (int i = 0; i < nbPlayers; i++) {
          swaitpid(clientHandlers[i], &status, 0);
      }
      // Attendre que le broadcaster termine
      swaitpid(childId2, &status, 0);
      
      // Nettoyage
      
      // Code de nettoyage des IPCs
      if (shm_id2 >= 0) {
          sshmdelete(shm_id2);
      }
      
      if (sem_id >= 0) {
          sem_delete(sem_id);
      }
      
      ret = sclose(pipefd[1]);
      sclose(sockfd);
  }
}