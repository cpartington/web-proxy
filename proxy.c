#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "csapp.h"
#include "cache.h"
#include "cbuf.h"
#include "sbuf.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_LISTEN_QUEUE 10		// max listen backlog size
#define MAX_LINE 8192 			// max text line length
#define MAX_RESPONSE 65536		// max HTTP response length
#define SBUF_SIZE 8				// max queue size

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
sbuf_t queue;
cbuf_t logger;
cache_t cache;

// Function declarations
void *handle_connection(void *vargp);
void *handle_log(void *vargp);
void handle(int fd);
void read_requesthdrs(rio_t * rp);
int parse_url(char *url, char *hostname, char *query);
int check_for_port(char *hostname);
void build_request(char* request, char* hostname, char* query);
int send_recv_message(unsigned char *request, int requestlen, unsigned char *response,
		char *host, int port);
void serve_static(int fd, char *filename, int filesize, char* query);
void get_filetype(char *filename, char *filetype);
void sigint_handler(int signal);
void clienterror(int fd, char *cause, char *errnum,
	    char *shortmsg, char *longmsg);

/* Main */
int main(int argc, char **argv)
{
    int listen_sock, conn_sock;
    socklen_t clientlen;
    struct sockaddr_storage client_addr;
    struct sockaddr_in server_addr;
    pthread_t tid;

	// Set up signals
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, sigint_handler);

	// Set up cache
	cache_init(&cache);
	
	// Set up thread queue
	sbuf_init(&queue, SBUF_SIZE);
	for (int i = 0; i < SBUF_SIZE; i++) {
		pthread_create(&tid, NULL, handle_connection, NULL);
	}

	// Set up logging queue
	cbuf_init(&logger, SBUF_SIZE);
	pthread_create(&tid, NULL, handle_log, NULL);

    // Open socket
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int));

    // Set up socket address information
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short) atoi(argv[1]));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Set up socket to listen
    bind(listen_sock, (struct sockaddr *) &server_addr, sizeof(server_addr));
    listen(listen_sock, MAX_LISTEN_QUEUE);

    while (1) {
    	clientlen = sizeof(struct sockaddr_in);
    	conn_sock = accept(listen_sock, (struct sockaddr *) &client_addr, &clientlen);
		sbuf_insert(&queue, conn_sock);
    }

	sbuf_deinit(&queue);
    return 0;
}

void *handle_connection(void *vargp) {
	while (1) {
		int fd = sbuf_remove(&queue);
		handle(fd);
	}
}

void *handle_log(void *vargp) {
	FILE *f;
	f = fopen("log.txt", "w");
	while (1) {
		char* message = cbuf_remove(&logger);
		printf("Logging request.\n");
		fprintf(f, "%s", message);
		free(message);
		printf("Done logging request.\n");
	}
	fclose(f);
}

/*
 * Handles an HTTP request/response transaction.
 */
void handle(int fd) {
	int is_local = 1;
	struct stat	sbuf;
	char buf[MAX_LINE], method[MAX_LINE], url[MAX_LINE], version[MAX_LINE];
	char hostname[MAX_LINE], query[MAX_LINE], filename[MAX_LINE];
	char request[MAX_LINE];
	char *response = malloc(MAX_RESPONSE * sizeof(char));
	rio_t rio;
	int port;

	// Read request line and headers
	Rio_readinitb(&rio, fd);
	if (!Rio_readlineb(&rio, buf, MAX_LINE)) {
		close(fd);
		return;
	}
	printf("%s", buf);
	sscanf(buf, "%s %s %s", method, url, version);
	if (strcasecmp(method, "GET") != 0) {
		clienterror(fd, method, "501", "Not Implemented", "Only GET method is implemented.");
		close(fd);
		return;
	}
	read_requesthdrs(&rio);

	// Parse URI from GET request
	is_local = parse_url(url, hostname, query);
	if ((port = check_for_port(hostname)) == 0) {
		port = 80;
	}

	// Prep message
	char *msg = malloc(sizeof(char) * MAX_LINE);

	// Check if request is cached
	char *object;
	printf("Checking in cache for existing request '%s'\n", url);
	if ((object = cache_find(&cache, url)) != NULL) {
		// Send response back to client
		int sent_bytes = 0;
		while (sent_bytes < strlen(object)) {
			sent_bytes += write(fd, object + sent_bytes, strlen(object) - sent_bytes);
		}
		sprintf(msg, "Request for %s. Found in cache.\n", url);
		close(fd);
		printf("Log: %s", msg);
		cbuf_insert(&logger, msg);
		return;
	}

	// Check type of GET request
	if (is_local) {
		// Local file
		strcpy(filename, ".");
		strcat(filename, query);
		// Check for valid file name
		if (stat(filename, &sbuf) < 0) {
			clienterror(fd, filename, "404", "Not found", "Couldn't find this file");
			close(fd);
			return;
		}
		// Serve static content from tiny
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
			clienterror(fd, filename, "403", "Forbidden", "Couldn't read the file");
			close(fd);
			return;
		}
		serve_static(fd, filename, sbuf.st_size, url);
		sprintf(msg, "Local file request for %s.\n", query);
	}
	else {
		// Build request
		build_request(request, hostname, query);
		// Send to url
		int response_len = send_recv_message((unsigned char *) request, strlen(request),
				(unsigned char *) response, hostname, port);
		
		// Send response back to client
		int sent_bytes = 0;
		while (sent_bytes < response_len) {
			sent_bytes += write(fd, response + sent_bytes, response_len - sent_bytes);
		}
		
		// Check for caching
		if (response_len < MAX_OBJECT_SIZE) {
			// Cache the response
			cache_add(&cache, response_len, url, response);
			printf("The size of the cache is now %d\n", cache.size);
		}
		sprintf(msg, "Foreign file request for %s. Sending to %s on port %d.\n", query, hostname, port);
	}
	close(fd);
	printf("Log: %s", msg);
	cbuf_insert(&logger, msg);
	return;
}

/**
 * Reads and prints the request headers.
 */
void read_requesthdrs(rio_t * rp) {
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
	while (strcmp(buf, "\r\n") != 0) {
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	}
	return;
}

/**
 * Parses URL into hostname and query args. Return 1 if local
 * content, 0 if another request is needed.
 */
int parse_url(char *url, char *hostname, char *query) {
	int h_start = 0;
	int h_end;

	if (url[0] == '/') {
		strcpy(hostname, "");
		strcpy(query, url);
		return 1;
	}
	else {
		// Check for http
		if (strncmp(url, "http", 4) == 0) {
			if (url[4] == 's')
				h_start += 8;
			else
				h_start += 7;
		}
		// Get hostname
		h_end = h_start;
		for (char* c = url + h_start; *c != '/'; c++) {
			h_end++;
		}
		memcpy(hostname, url + h_start, h_end - h_start);
		strcpy(query, url + h_end);
		return 0;
	}
}

int check_for_port(char *hostname) {
	char port[10];
	for (int i = 0; i < strlen(hostname); i++) {
		if (hostname[i] == ':') {
			strcpy(port, hostname + i + 1);
			hostname[i] = '\0';
			return atoi(port);
		}
	}
	return 0;
}

void build_request(char* request, char* hostname, char* query) {
	// Request line
	strcpy(request, "GET ");
	strcat(request, query);
	strcat(request, " HTTP/1.0\r\n");
	// Headers
	strcat(request, "Host: ");
	strcat(request, hostname);
	strcat(request, "\r\n");
	strcat(request, user_agent_hdr);
	strcat(request, "Connection: close\r\n");
	strcat(request, "Proxy-Connection: close\r\n");
	strcat(request, "\r\n");
}

int send_recv_message(unsigned char *request, int requestlen, unsigned char *response,
		char *host, int port) {
	/*
	 * Send a message (request) over UDP to a server (server) and port
	 * (port) and wait for a response, which is placed in another byte
	 * array (response).  Create a socket, "connect()" it to the
	 * appropriate destination, and then use send() and recv();
	 *
	 * INPUT:  request: a pointer to an array of bytes that should be sent
	 * INPUT:  requestlen: the length of request, in bytes.
	 * INPUT:  response: a pointer to an array of bytes in which the
	 *             response should be received
	 * OUTPUT: the size (bytes) of the response received
	 */
	int sock;
	struct sockaddr_in address;
	struct hostent *host_id;

	// Open socket
	sock = socket(AF_INET, SOCK_STREAM, 0);

	// Fill in socket address
	memset(&address, 0, sizeof(struct sockaddr_in)); // Set the entire address to 0
	address.sin_family = AF_INET;
	address.sin_port = htons((ushort) port);
	host_id = gethostbyname(host);
	memcpy(&address.sin_addr, host_id->h_addr_list[0], host_id->h_length);

	// Connect to server
	connect(sock,(struct sockaddr *) &address, sizeof(address));

	// Send message
	int sent_bytes = 0;
	while (sent_bytes < requestlen) {
		sent_bytes += write(sock, request + sent_bytes, requestlen - sent_bytes);
	}

	// Receive response
	int read_bytes = 0;
	read_bytes = recv(sock, response, MAX_RESPONSE, MSG_WAITALL);

	// Close socket
	close(sock);

	// Return size of response
	return read_bytes;
}

/*
 * serve_static - copy a file back to the client
 */
void serve_static(int fd, char *filename, int filesize, char *url) {
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXBUF];
	char *masterbuf = malloc(MAX_RESPONSE * sizeof(char));

	/* Send response headers to client */
	get_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	Rio_writen(fd, buf, strlen(buf));
	strcpy(masterbuf, buf);

	/* Send response body to client */
	srcfd = Open(filename, O_RDONLY, 0);
	srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
	Close(srcfd);
	Rio_writen(fd, srcp, filesize);
	strcat(masterbuf, srcp);
	Munmap(srcp, filesize);

	// Cache it
	if (strlen(masterbuf) < MAX_OBJECT_SIZE) {
		// Cache the response
		cache_add(&cache, strlen(masterbuf), url, masterbuf);
		printf("The size of the cache is now %d\n", cache.size);
	}
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) {
	if (strstr(filename, ".html"))
		strcpy(filetype, "text/html");
	else if (strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
	else if (strstr(filename, ".png"))
		strcpy(filetype, "image/png");
	else if (strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpeg");
	else
		strcpy(filetype, "text/plain");
}

void sigint_handler(int signal) {
	sbuf_deinit(&queue);
	cbuf_deinit(&logger);
	cache_deinit(&cache);
	exit(-1);
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
	    char *shortmsg, char *longmsg) {
	char buf[MAXLINE], body[MAXBUF];

	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=" "ffffff" ">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}
