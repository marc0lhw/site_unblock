/* 
 * usage: tcpserver <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSIZE 100000

struct THREAD_DATA{
        int _childfd;
};

/*
 * error - wrapper for perror
 */
void error(const char *msg) {
  	perror(msg);
  	exit(1);
}

void getHost(char *hostname, char* buf, int size) {
        const char *GET = "GET ";
        const char *POST = "POST ";
        const char *HEAD = "HEAD ";
        const char *PUT = "PUT ";
        const char *DELETE = "DELETE ";
        const char *OPTIONS = "OPTIONS ";
        const char *Host = "Host: ";
	int i;

        if( memcmp(buf, GET, 4) == 0 || memcmp(buf, PUT, 4) == 0 || memcmp(buf, POST, 5) == 0 ||
        memcmp(buf, HEAD, 5) == 0 || memcmp(buf, DELETE, 7) == 0 || memcmp(buf, OPTIONS, 8) == 0 ) {
                char *ptr = (char *)memmem(buf, size, Host, 6);
                if(ptr == NULL) return;
                for (i=0; i< size -1; i++){
                        if(ptr[i] == 0x0d && ptr[i+1] == 0x0a)
                                break;
                }
                ptr += strlen(Host);

		memcpy(hostname, ptr, i-strlen(Host)); 

		return;
	}
//	printf("Can't found http methods\n");
	
        return;
}

int Adddummy(char * fakebuf, char * buf, int size) {

	const char * dummy = "GET / HTTP/1.1\r\nHost: fake.dummy.com\r\n\r\n";
	int dummylen = strlen(dummy);

	memcpy(fakebuf, dummy, dummylen);
	memcpy(fakebuf + dummylen, buf, size);

//	printf("fakebuf GET : \n%s", fakebuf);

	return size + dummylen;
}

void *t_receive_packet(void *th_data)
{
  	int childfd; /* child socket */
	int Tosssockfd;
	struct hostent *Tossserver;
	struct sockaddr_in Tossserveraddr;
	int Tossportno = 80;
  	char buf[BUFSIZE]; /* message buffer */
  	int n; /* message byte size */
	int shift=0;
	
	struct THREAD_DATA * arg = (struct THREAD_DATA *)th_data;
	childfd = arg->_childfd;

    	bzero(buf, BUFSIZE);
//	read requests
	do {
	    	n = read(childfd, buf+shift, BUFSIZE-shift);
	    	if (n < 0)
	      		error("ERROR reading from socket");
		if (n == 0) break;

		void *ptr = memmem(buf, BUFSIZE, "\r\n\r\n", 4);
    		if ( ptr != NULL) break;
		
//		printf("Thread read %d bytes from client\n", n);
		shift += n;
	} while ( shift < BUFSIZE );

    	char hostname[100] = "";
    	getHost(hostname, buf, shift==0? n:shift);
//	printf("host : \n%s\n", hostname);

    	Tosssockfd = socket(AF_INET, SOCK_STREAM, 0);
    	if (Tosssockfd < 0)
        	error("ERROR opening socket");
	Tossserver = gethostbyname(hostname);
    	if (Tossserver == NULL) {
//        	printf("ERROR, no such host as %s\n", hostname);
//		printf("buf : \n%s\n", buf);
		close(Tosssockfd);
		close(childfd);
		return NULL;
	}

	bzero((char *) &Tossserveraddr, sizeof(Tossserveraddr));
   	Tossserveraddr.sin_family = AF_INET;
    	bcopy((char *)Tossserver->h_addr, (char *)&Tossserveraddr.sin_addr.s_addr, Tossserver->h_length);
    	Tossserveraddr.sin_port = htons(Tossportno);

//	connect with server
    	if (connect(Tosssockfd, (const sockaddr*)&Tossserveraddr, sizeof(Tossserveraddr)) < 0)
      		error("ERROR connecting");

//	make fakebuf
	char fakebuf[BUFSIZE];
	int fakebuflen = Adddummy(fakebuf, buf, shift==0? n:shift);

    	n = write(Tosssockfd, fakebuf, fakebuflen);
    	if (n < 0)
      		error("ERROR writing to socket");

	while(1){
	        bzero(buf, BUFSIZE);
		n = read(Tosssockfd, buf, BUFSIZE);
		if ( n == 0 || n == -1) break;
//   		printf("Echo from server: %d bytes\n", n);

//		skip error response
		if ( memmem(buf, n, " 400 Bad Request\r\n", 18) || memmem(buf, n, " 404 Not Found\r\n", 16)  ||
			memmem(buf, n, "409 Conflict\r\n", 14) )
			continue;	
    
    		n = write(childfd, buf, n);
//		printf("Thread write %d byte to client\n", n);
    		if (n < 0){
//     			error("ERROR writing to socket");
			perror("write");
			break;
		}
	}
	close(Tosssockfd);
    	close(childfd);
//	printf("[*] Thread Finish\n\n");
}

int main(int argc, char **argv) {
  	int parentfd; /* parent socket */
  	int childfd; /* child socket */
  	int portno; /* port to listen on */
  	int clientlen; /* byte size of client's address */
  	struct sockaddr_in serveraddr; /* server's addr */
  	struct sockaddr_in clientaddr; /* client addr */
  	struct hostent *hostp; /* client host info */
  	char buf[BUFSIZE]; /* message buffer */
  	char *hostaddrp; /* dotted decimal host addr string */
  	int optval; /* flag value for setsockopt */
  	int n; /* message byte size */

// 	check command line arguments 
  	if (argc != 2) {
    		fprintf(stderr, "usage: %s <port>\n", argv[0]);
    		exit(1);
  	}
  	portno = atoi(argv[1]);

// 	socket: create the parent socket 
  	parentfd = socket(AF_INET, SOCK_STREAM, 0);
  	if (parentfd < 0) error("ERROR opening socket");

  	optval = 1;
  	setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

  	bzero((char *) &serveraddr, sizeof(serveraddr));

  	serveraddr.sin_family = AF_INET;
  	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  	serveraddr.sin_port = htons((unsigned short)portno);

// 	bind: associate the parent socket with a port 
  	if (bind(parentfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) 
    		error("ERROR on binding");

// 	listen: make this socket ready to accept connection requests 
    	if (listen(parentfd, 10) < 0) 		// allow 10 requests to queue up
      		error("ERROR on listen");
    	clientlen = sizeof(clientaddr);

  	while (1) {

// 		accept: wait for a connection request 
    		childfd = accept(parentfd, (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen);
    		if (childfd < 0) error("ERROR on accept");
    
    		hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
			  sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    		if (hostp == NULL) error("ERROR on gethostbyaddr");
    		hostaddrp = inet_ntoa(clientaddr.sin_addr);
    		if (hostaddrp == NULL) error("ERROR on inet_ntoa\n");
//    		printf("server established connection with %s (%s)\n", hostp->h_name, hostaddrp);


// 		thread detach올리기
       		pthread_t p_thread;
        	struct THREAD_DATA th_data;
        	th_data._childfd = childfd;

        	int thr_id = pthread_create(&p_thread, NULL, t_receive_packet, (void *)&th_data);
        	if (thr_id < 0)
        	{
                	perror("thread create error : ");
                	exit(0);
        	}
        	pthread_detach(p_thread);
//        	printf("[*] Make Thread\n\n");
  	}

  	close(parentfd);
}
