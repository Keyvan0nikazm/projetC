#ifndef _HEADER_H_
#define _HEADER_H_

#define SERVER_PORT 15563
#define SERVER_IP "127.0.0.1" /* localhost */
#define MAX_PSEUDO 256

// --- Ajouts pour le serveur ---
#define MAX_PLAYERS 2
#define BACKLOG 5
#define TAILLE 1024

#include <signal.h>

typedef struct Player
{
  char pseudo[MAX_PSEUDO];
  int sockfd;
} Player;

extern volatile sig_atomic_t end;
extern volatile sig_atomic_t registrationTimeout;

void endServerHandler(int sig);
void alarmHandler(int sig);
void startRegistrationTimer();
void terminate(Player *tabPlayers, int nbPlayers);
int initSocketServer(int serverPort);
int initSocketClient(char * serverIP, int serverPort);
// --- fin ajouts ---

// Clés pour les ressources IPC
#define SHM_KEY 248
#define SEM_KEY 369

#define PERM 0666

typedef enum
{
  INSCRIPTION_REQUEST = 10,
  INSCRIPTION_OK = 11,
  INSCRIPTION_KO = 12
} Code;

#endif
