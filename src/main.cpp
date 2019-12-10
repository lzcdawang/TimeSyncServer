#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct __attribute__((__packed__)) TimeRequest
{
    char protocol[3]; // Protocol name (TSP)
    uint8_t protocolVersion; // 1
    char unused[4]; // 8 bytes padding, can have future use
    uint64_t clientCookie; // 8 bytes which user can set to whatever value, and will be returned in reply
};

const int TimeRequestPacketSize = sizeof(TimeRequest);


struct __attribute__((__packed__)) TimeReply
{
    char protocol[3]; // Protocol name (TSP)
    uint8_t protocolVersion; // 1
    char unused[4]; // 8 bytes padding, can have future use
    uint64_t clientCookie; // the cookie which was sent in the request, copied to the reply for reference
    uint64_t timeSinceEphoc1970Ms; // number of ms since ephoc time - 1 Jan 1970 GMT
};

const int TimeReplyPacketSize = sizeof(TimeReply);

/*
 * error - wrapper for perror
 */
void error(const char *msg) {
  // perror(msg);
  exit(1);
}

static void daemonize()
{
	pid_t pid = 0;
	int fd;

	// become background proccess
	pid = fork();
	if (pid < 0) // fork failed
  { 
		exit(EXIT_FAILURE);
	}
	if (pid > 0) // parent terminates
  { 
		exit(EXIT_SUCCESS);
	}

  // become leader of new session
	if (setsid() < 0) 
  {
		exit(EXIT_FAILURE);
	}

	// Ignore signal sent from child to parent process
	signal(SIGCHLD, SIG_IGN);

	// ensure we are not session leader with second fork
	pid = fork();
	if (pid < 0) // second fork failed
  {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) // parent terminates
  {
		exit(EXIT_SUCCESS);
	}

	// clear file mode creation mask
	umask(0);

  // change to root directory
	chdir("/");

  // close all file descriptors
  int maxfd = sysconf(_SC_OPEN_MAX);
  if (maxfd == -1) // limit is indeterminate...
  {
    maxfd = 8192; // so take a guess
  }
  for (fd = 0; fd < maxfd; fd++)
  {
    close(fd);
  }

  // reopen standard fd's to /dev/null
  close(STDIN_FILENO);

  fd = open("/dev/null", O_RDWR);
  if (fd != STDIN_FILENO) // 'fd' should be 0
  {
    exit(EXIT_FAILURE);
  }
  if (dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
  {
    exit(EXIT_FAILURE);
  }
  if (dup2(STDIN_FILENO, STDERR_FILENO) != STDERR_FILENO)
  {
    exit(EXIT_FAILURE);
  }
  
}


int main(int argc, char **argv) {
  int sockfd; /* socket */
  int portno; /* port to listen on */
  socklen_t clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  char requestBuffer[TimeRequestPacketSize];
  char replyBuffer[TimeReplyPacketSize];
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */

  const char *appName = argv[0];

  /* 
   * check command line arguments 
   */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);

  daemonize();

	/* Open system log and write message to it */
	openlog(argv[0], LOG_PID|LOG_CONS, LOG_DAEMON);
	syslog(LOG_INFO, "Started time sync server '%s'", appName);  

  /* 
   * socket: create the parent socket 
   */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));

  /*
   * build the server's Internet address
   */
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)portno);

  /* 
   * bind: associate the parent socket with a port 
   */
  if (bind(sockfd, (struct sockaddr *) &serveraddr, 
	   sizeof(serveraddr)) < 0) 
    error("ERROR on binding");

  /* 
   * main loop: wait for a datagram, check validite and response with the time
   */
  clientlen = sizeof(clientaddr);
  while (1) {

    /*
     * recvfrom: receive a UDP datagram from a client
     */
    bzero(requestBuffer, TimeRequestPacketSize);
    n = recvfrom(sockfd, requestBuffer, TimeRequestPacketSize, 0,
		 (struct sockaddr *) &clientaddr, &clientlen);
    if (n < 0)
      error("ERROR in recvfrom");

    struct timeval tv;
    gettimeofday(&tv, NULL);
    // convert sec to ms and usec to ms
    uint64_t curr_time_ms_since_epoch = ((uint64_t)(tv.tv_sec)) * 1000 + ((uint64_t)(tv.tv_usec)) / 1000;

    memcpy(replyBuffer, requestBuffer, TimeRequestPacketSize);
    ((TimeReply *)replyBuffer)->timeSinceEphoc1970Ms = curr_time_ms_since_epoch;
    n = sendto(sockfd, replyBuffer, TimeReplyPacketSize, MSG_CONFIRM, (struct sockaddr *) &clientaddr, clientlen);
    if (n < 0) 
      error("ERROR in sendto");
  }

	syslog(LOG_INFO, "Stopped %s", appName);

}