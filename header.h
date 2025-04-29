#ifndef _HEADER_H_
#define _HEADER_H_

#define SERVER_PORT 15563
#define SERVER_IP "127.0.0.1" /* localhost */
#define MAX_PSEUDO 256

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
