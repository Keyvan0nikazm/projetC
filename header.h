#ifndef _HEADER_H_
#define _HEADER_H_

#define SERVER_PORT 15563
#define SERVER_IP "127.0.0.1" /* localhost */
#define MAX_PSEUDO 256

typedef enum
{
  INSCRIPTION_REQUEST = 10,
  INSCRIPTION_OK = 11,
  INSCRIPTION_KO = 12
} Code;

#endif
