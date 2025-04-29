#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "header.h"
#include "utils_v3.h"

#define SHM_KEY 248
#define SEM_KEY 369
#define MAX_PLAYERS 2
#define BACKLOG 5

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
    sclose(tabPlayers[i].sockfd);
  }
  exit(0);
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
  // GET SEMAPHORE
  int sem_id = sem_get(SEM_KEY, 0);
  // GET SHARED MEMORY
  int shm_id = sshmget(SHM_KEY, sizeof(int), 0);

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
          terminate(tabPlayers, nbPlayers); // Gracefully terminate on timeout
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
      terminate(tabPlayers, nbPlayers); // Gracefully terminate on timeout
    }
  }

  sclose(sockfd);
}