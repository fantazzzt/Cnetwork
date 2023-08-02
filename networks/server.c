#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#define PORT "3490"
#define BACKLOG 10
#define MAXDATASIZE 100
#define MAXCLIENTS 100

void sigchld_handler(int s) {
  int saved_errno = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0);
  errno = saved_errno;
}

void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void send_to_all(int clients[], int csize, int sender, char *msg, int msglen) {
  for(int i = 0; i < csize; i++) {
    if(clients[i] != sender) {
      send(clients[i], msg, msglen, 0);
    }
  }
}

int main(void) {
  int sockfd, new_fd, fdmax, i;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  struct sigaction sa;
  int yes = 1;
  char s[INET6_ADDRSTRLEN];
  int rv;

  fd_set master;
  fd_set read_fds;
  int clients[MAXCLIENTS];
  int nbytes;
  char buf[MAXDATASIZE];
  
  memset(clients, -1, sizeof(clients));
  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("server: socket");
      continue;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("setsockopt");
      exit(1);
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("server: bind");
      continue;
    }

    break;
  }

  freeaddrinfo(servinfo);

  if (p == NULL)  {
    fprintf(stderr, "server: failed to bind\n");
    exit(1);
  }

  if (listen(sockfd, BACKLOG) == -1) {
    perror("listen");
    exit(1);
  }

  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("server: waiting for connections...\n");
  
  FD_SET(sockfd, &master);
  fdmax = sockfd;

  while(1) {
    read_fds = master;
    if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
      perror("select");
      exit(1);
    }

    for(i = 0; i <= fdmax; i++) {
      if (FD_ISSET(i, &read_fds)) {
        if (i == sockfd) {
          sin_size = sizeof their_addr;
          new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
          if (new_fd == -1) {
            perror("accept");
          } else {
            FD_SET(new_fd, &master);
            if (new_fd > fdmax) {
              fdmax = new_fd;
            }
            for(int j = 0; j < MAXCLIENTS; j++) {
              if(clients[j] < 0) {
                clients[j] = new_fd;
                break;
              }
            }
            printf("server: new connection from %s on socket %d\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr*)&their_addr), s, sizeof s), new_fd);
          }
        } else {
          if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
            if (nbytes == 0) {
              printf("server: socket %d hung up\n", i);
            } else {
              perror("recv");
            }
            close(i);
            FD_CLR(i, &master);
            for(int j = 0; j < MAXCLIENTS; j++) {
              if(clients[j] == i) {
                clients[j] = -1;
                break;
              }
            }
          } else {
            buf[nbytes] = '\0';
            printf("server: received %s from socket %d\n", buf, i);
            send_to_all(clients, MAXCLIENTS, i, buf, nbytes);
          }
        }
      }
    }
  }
  return 0;
}
