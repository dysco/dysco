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

int MAX = 65535;
int N = 1000;

void test_init(int *sockfd)
{
  int i;

  for (i = 0; i < N; i++) {
    sockfd[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd[i] < 0) {
      error("ERROR opening socket");
    }
  }
}

void test_teardown(int *sockfd)
{
  int i;

  for (i = 0; i < N; i++ ) {
    close(sockfd[i]);
  }
}

void test_run(int *sockfd, struct sockaddr_in *serv_addr)
{
  int i;
  //struct timeval start, end;

  for (i = 0; i < N; i++) {
    //gettimeofday(&start, NULL);
    if (connect(sockfd[i], (struct sockaddr *) serv_addr, sizeof(*serv_addr)) < 0) {
      error("ERROR connecting");
    }
    //gettimeofday(&end, NULL);
    //printf("%lf\n", ((end.tv_sec - start.tv_sec)*1.0E+3 + (end.tv_usec - start.tv_usec)*1.0E-3)/N);
  }
}

int main(int argc, char *argv[])
{
  int sockfd[MAX], portno;
  struct sockaddr_in serv_addr;
  struct hostent *server;

  if (argc < 4) {
    fprintf(stderr,"usage %s <hostname> <port> <# of connections>\n", argv[0]);
    exit(1);
  }

  N = atoi(argv[3]);
  
  server = gethostbyname(argv[1]);
  if (server == NULL) {
    fprintf(stderr, "ERROR, no such host\n");
    exit(0);
  }
  portno = atoi(argv[2]);
  init_sockaddr(&serv_addr, server, portno);

  test_init(sockfd);
  test_run(sockfd, &serv_addr);

  printf("Now we have %d established connections. Push any keys to close: ", N);
  getchar();
  
  test_teardown(sockfd);

  return 0;
}
