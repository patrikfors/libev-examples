/*
 * The chat server example from libevent rewritten for libev and winsock
 */

/*
 * License notice from the libevent example this was based on.
 * 
 * Copyright (c) 2011, Jason Ish
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * A simple chat server using libev.
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h> /* socklen_t */

/* Need to link with Ws2_32.lib */
#pragma comment(lib, "ws2_32.lib")


#include <io.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

/* On some systems (OpenBSD/NetBSD/FreeBSD) you could include
 * <sys/queue.h>, but for portability we'll include the local copy. */
#include "queue.h"

/* Libev. */
#include "libev.h"

/* Port to listen on. */
#define SERVER_PORT 5555

/*
 * A struct for client specific data.
 *
 * This also includes the tailq entry item so this struct can become a
 * member of a tailq - the linked list of all connected clients.
 */
struct client_t {
	/*
	* This struct wraps the io watcher.
	* The watcher has to be first entry in the struct to be able to
	* cast from client_t to ev_io
	*/
	ev_io io;

	/*
	 * It is a bit of a pain to use libev with winsock since libev requires
	 * file descriptors while all of winsock's socket functions requires
	 * sockets to be of type SOCKET. On real OS's they are just the same.
	 * To work around that, the SOCKET socket is stored in the io wrappers
	 * for both client and server. The file descriptor that libev uses is
	 * stored in the ev_io struct.
	*/
	SOCKET socketd;

	/*
	* This holds the pointers to the next and previous entries in
	* the tail queue.
	*/
	TAILQ_ENTRY (client_t) entries;
};

struct server_t {
	/*
	* This struct wraps the io watcher.
	* The watcher has to be first entry in the struct to be able to
	* cast from client_t to ev_io
	*/
	ev_io io;

	/*
	* The SOCKET socket.
	*/
	SOCKET socketd;
};

/*
 * The head of our tailq of all connected clients.  This is what will
 * be iterated to send a received message to all connected clients.
 */
TAILQ_HEAD (, client_t) client_tailq_head;

/*
* perror for winsock errors
*/
void socket_perror (char const* message)
{
	char* buffer;
	FormatMessageA (FORMAT_MESSAGE_ALLOCATE_BUFFER |
	                FORMAT_MESSAGE_FROM_SYSTEM,
	                0, WSAGetLastError(), 0, (char*) &buffer, 0, 0);
	fprintf (stderr, "%s: %s\n", message, buffer);
	LocalFree (buffer);
}

/*
 * Called by libev when there is data to read.
 */
void read_cb (struct ev_loop* loop, struct ev_io* watcher, int revents)
{
	struct client_t* client;
	struct client_t* this_client = (struct client_t*) watcher;

	uint8_t buffer[8192];
	SSIZE_T read;

	/* Read 8k at a time and send it to all connected clients. */
	for (;;) {
		/* Receive message from client socket */
		read = recv (this_client->socketd,
		             (char*) buffer,
		             _countof (buffer),
		             0);

		if (read < 0) {
			if (WSAEWOULDBLOCK ==  WSAGetLastError()) {
				/* Done. */
				break;
			}
		}

		if (read <= 0) {
			ev_io_stop (loop, &this_client->io);

			/* Remove the client from the tailq. */
			TAILQ_REMOVE (&client_tailq_head, 
					this_client, 
					entries);

			closesocket (this_client->socketd);
			free (this_client);
			return;
		}


		TAILQ_FOREACH (client, &client_tailq_head, entries) {
			if (client != this_client) {
				send (client->socketd, 
						(void*) buffer, 
						read, 
						0);
			}
		}
	}
}

/*
 * This function will be called by libev when there is a connection
 * ready to be accepted.
 */
void accept_cb (struct ev_loop* loop, struct ev_io* watcher_, int revents)
{
	struct server_t* w = (struct server_t*) watcher_;
	SOCKET client_socket;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof (client_addr);
	struct client_t* client = NULL;

	if (EV_ERROR & revents) {
		perror ("Got invalid event");
		return;
	}

	/* Accept client request */
	client_socket = accept (
	                    w->socketd,
	                    (struct sockaddr*) &client_addr,
	                    &client_len);
	if (INVALID_SOCKET == client_socket) {
		socket_perror ("Accept error");
		return;
	}

	/* We've accepted a new client, create a client object. */
	client = (struct client_t*) calloc (1, sizeof (*client));

	if (client == NULL) {
		perror ("malloc failed");
		return;
	}
	client->socketd = client_socket;

	/* Initialize and start watcher to read client requests */
	ev_io_init (
	    &client->io,
	    read_cb,
	    _open_osfhandle (client_socket, 0),
	    EV_READ);

	/* Add the new client to the tailq. */
	TAILQ_INSERT_TAIL (&client_tailq_head, client, entries);

	ev_io_start (loop, &client->io);

	printf ("Accepted connection from %s\n",
	        inet_ntoa (client_addr.sin_addr));
}

int
main (int argc, char** argv)
{
	struct WSAData wsa_data;
	u_long nonblock_mode = 1;
	int reuseaddr_mode = 1;
	struct sockaddr_in listen_addr;
	SOCKET listen_socket;
	int listen_fd;
	struct ev_loop* loop;
	struct server_t w_accept;

	WSAStartup (MAKEWORD (2, 2), &wsa_data);

	/* Initialize the tailq. */
	TAILQ_INIT (&client_tailq_head);

	listen_socket = socket (AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == listen_socket) {
		socket_perror ("socket");
		return EXIT_FAILURE;
	}

	if (0 != ioctlsocket (listen_socket, FIONBIO, &nonblock_mode)) {
		socket_perror ("ioctlsocket");
		return EXIT_FAILURE;
	}

	if (0 != setsockopt (
	        listen_socket, SOL_SOCKET, SO_REUSEADDR,
	        (void*) &reuseaddr_mode, sizeof (reuseaddr_mode))) {
		socket_perror ("setsockopt");
		return EXIT_FAILURE;
	}

	memset (&listen_addr, 0, sizeof (listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_port = htons (SERVER_PORT);
	listen_addr.sin_addr.s_addr = INADDR_ANY;

	/* Bind socket to address */
	if (0 != bind (
	        listen_socket,
	        (struct sockaddr*) &listen_addr,
	        sizeof (listen_addr))) {
		socket_perror ("bind");
		return EXIT_FAILURE;
	}

	/* Start listening on the socket */
	if (0 != listen (listen_socket, 5)) {
		socket_perror ("listen");
		return EXIT_FAILURE;
	}

	listen_fd = _open_osfhandle (listen_socket, 0);
	loop = ev_default_loop (0);

	w_accept.socketd = listen_socket;

	ev_io_init (&w_accept.io, accept_cb, listen_fd, EV_READ);
	ev_io_start (loop, &w_accept.io);

	/* Start infinite loop */
	for (;;) {
		ev_loop (loop, 0);
	}

	WSACleanup();
	return EXIT_SUCCESS;
}
