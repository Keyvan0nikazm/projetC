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

#define TAILLE 1024

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

int main(int argc, char *argv[]) {
  printf("Bienvenue dans le programme d'inscription au serveur de jeu\n");

  int sockfd = initSocketClient(SERVER_IP, SERVER_PORT);

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

  int childId = sfork();
  if (childId == 0){
    printf("Je suis le processus PAC-MAN-IPL (pid: %d)\n", getpid());
    
    // Redirect both stdin and stdout to the socket
    dup2(sockfd, STDIN_FILENO);
    dup2(sockfd, STDOUT_FILENO);

    // Exécuter pas-cman-ipl
    printf("Lancement de l'interface graphique pas-cman-ipl...\n");
    execl("./target/release/pas-cman-ipl", "pas-cman-ipl", NULL);

    // This code is only reached if execl fails
    perror("Error executing pas-cman-ipl");
    exit(EXIT_FAILURE);
  }else{
    printf("Je suis le processus parent (pid: %d)\n", getpid());

    int status;
    swaitpid(childId, &status, 0);
  }

  sclose(sockfd);
  return 0;
}