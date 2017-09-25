#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/time.h>

#include "lib_socket.h"

void error(const char *msg)
{
  perror(msg);
  exit(0);
}

void init_sockaddr(struct sockaddr_in *addr, struct hostent *host, int portno)
{
  memset(addr, 0, sizeof(*addr));
  addr->sin_family = AF_INET;
  bcopy((char *)host->h_addr, (char *)&addr->sin_addr.s_addr, host->h_length);
  addr->sin_port = htons(portno);
}
