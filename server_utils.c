#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include "header.h"
#include "utils_v3.h"

volatile sig_atomic_t end = 0;
volatile sig_atomic_t registrationTimeout = 0;

void endServerHandler(int sig)
{
  end = 1;
}

void alarmHandler(int sig)
{
  registrationTimeout = 1;
  printf("Temps d'inscription écoulé. Fermeture du serveur...\n");
}

void startRegistrationTimer()
{
  alarm(30);
}

void terminate(Player *tabPlayers, int nbPlayers)
{
  printf("\nJoueurs inscrits : \n");
  for (int i = 0; i < nbPlayers; i++)
  {
    printf("  - Client %d inscrit\n", i + 1);
    const char *message = "Temps d'inscription écoulé. Fermeture du serveur.\n";
    nwrite(tabPlayers[i].sockfd, message, strlen(message));
  }
  end = 1;
}

int initSocketServer(int serverPort)
{
  struct sockaddr_in addr;

  int sockfd = ssocket();
  sbind(SERVER_PORT, sockfd);
  slisten(sockfd, BACKLOG);

  return sockfd;
}

/**
 * PRE: serverIP : a valid IP address
 *      serverPort: a valid port number
 * POST: on success, connects a client socket to serverIP:serverPort
 *       on failure, displays error cause and quits the program
 * RES: return socket file descriptor
 */
int initSocketClient(char * serverIP, int serverPort)
{
  int sockfd = ssocket();
  sconnect(serverIP, serverPort, sockfd);
  return sockfd;
}
