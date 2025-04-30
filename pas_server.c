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

  sem_create(SEM_KEY, 1, PERM, 0);

  struct GameState gameState;

  // if (*z == 0) {;
  //   FileDescriptor fdmap = sopen("./resources/map.txt", O_RDONLY, 0);
  //   FileDescriptor sout = 1;
  //   load_map(fdmap, sout, &gameState);
  //   sclose(fdmap);
  //   *z = 1;
  // }

  int pipefd[2];
  int ret = spipe(pipefd);
  
  int childId = sfork();
  if (childId == 0) {
      // PREMIER ENFANT (client-handler)
      bool valid = true;
      ret = sclose(pipefd[0]);
      printf("Je suis le client-handler (pid: %d)\n", getpid());
      fflush(stdout);
      
      // Code du client-handler
      
      ret = sclose(pipefd[1]);
      exit(0);
  } 
  else {
      // PARENT OU DEUXIÈME ENFANT
      int childId2 = sfork();
      if (childId2 == 0) {
          // DEUXIÈME ENFANT (broadcaster)
          ret = sclose(pipefd[1]);
          printf("Je suis le broadcaster (pid: %d)\n", getpid());
          fflush(stdout);
          
          // Code du broadcaster
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
      }
      else {
          // PARENT
          ret = sclose(pipefd[0]); // Ferme l'extrémité de lecture
          printf("Je suis le parent (pid: %d)\n", getpid());
          fflush(stdout);
          
          // Chargement de la carte
          FileDescriptor fdmap = sopen("./resources/map.txt", O_RDONLY, 0);
          load_map(fdmap, pipefd[1], &gameState);
          sclose(fdmap);
          
          // Attendre que les enfants terminent
          int status;
          printf("Parent: attente de la fin des processus enfants...\n");
          fflush(stdout);
          swaitpid(childId, &status, 0);
          swaitpid(childId2, &status, 0);
          
          // Nettoyage
          printf("Parent: nettoyage des ressources...\n");
          fflush(stdout);
          
          // Code de nettoyage des IPCs
          int shm_id2 = sshmget(SHM_KEY, 2 * sizeof(pid_t), 0);
          if (shm_id2 >= 0) {
              sshmdelete(shm_id2);
          }
          
          int sem_id = sem_get(SEM_KEY, 1);
          if (sem_id >= 0) {
              sem_delete(sem_id);
          }
          
          printf("IPCs freed.\n");
          fflush(stdout);
          
          ret = sclose(pipefd[1]);
          sclose(sockfd);
      }
  }
}