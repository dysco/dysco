#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "lib_socket.h"

int N = 1000 + 10;

int main(int argc, char *argv[])
{
  int listenfd, sockfd[N], portno;
  struct sockaddr_in serv_addr;
  int i = 0, on = 1;

  if (argc < 2) {
    fprintf(stderr,"ERROR, no port provided\n");
    exit(1);
  }

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    error("ERROR opening socket");
  }

  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
    error("ERROR failed to reuse socket");
  }

  memset(&serv_addr, 0, sizeof(serv_addr));
  portno = atoi(argv[1]);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);

  if (bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    error("ERROR on binding");
  }

  listen(listenfd, N);

  while(1) {
    for (i = 0; i < N; ++i) {
      sockfd[i] = accept(listenfd, (struct sockaddr*)NULL, NULL); 
      if (sockfd[i] < 0) {
	error("ERROR on accept");
      }
    }

    for (i = 0; i < N; i++) {
      close(sockfd[i]);
    }
  }

  close(listenfd);

  return 0; 
}
