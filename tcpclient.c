/**
	Simple TCP client to fetch a web page
*/

#include <stdio.h>
#include <string.h> //strlen
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

char colors[8][10] = {
		KNRM,
		KRED,
		KGRN,
		KYEL,
		KBLU,
		KMAG,
		KCYN,
		KWHT};

// #define BUFSIZ BUFSIZE

void stop(char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

#define NTP_TIMESTAMP_DELTA 2208988800ull

#define LI(packet) (uint8_t)((packet.li_vn_mode & 0xC0) >> 6)		// (li   & 11 000 000) >> 6
#define VN(packet) (uint8_t)((packet.li_vn_mode & 0x38) >> 3)		// (vn   & 00 111 000) >> 3
#define MODE(packet) (uint8_t)((packet.li_vn_mode & 0x07) >> 0) // (mode & 00 000 111) >> 0

void error(char *msg)
{
	perror(msg); // Print the error message to stderr.

	exit(0); // Quit the process.
}

void get_time(char **);
void print_interface(int, char *, int);
void parse_message(char *, char **, char **);
void print_new_message(char *, char **);
char *get_token(int, char *);

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		printf("Il faut 2 arguments, l'ip et le port du server");
		return 0;
	}

	int socket_desc;
	struct sockaddr_in server;
	char *message, server_reply[512];

	//Create socket
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket");
	}

	server.sin_addr.s_addr = inet_addr(argv[1]);
	server.sin_family = AF_INET;
	server.sin_port = htons(atoi(argv[2])); // Regarder dans la correction du prof, atoi est déprécié (on doit utiliser sscanf)

	// gestion du pseudo
	char pseudo[256];
	printf("Quel est votre pseudo ?\n");
	scanf("%s", pseudo);
	printf("Bienvenue %s\n\n", pseudo);

	//Connect to remote server
	if (connect(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
	{
		puts("connect error");
		return 1;
	}

	puts("Connected\n");

	if (send(socket_desc, pseudo, strlen(pseudo), 0) == -1)
		stop("send()");

	printf("--- Lancement du chat ---\n\n");

	int n = 0;
	// Receive a reply from the server
	// faire une boucle non bloquante et regarder le clavier et le réseau
	fcntl(socket_desc, F_SETFL, fcntl(socket_desc, F_GETFL) | O_NONBLOCK);
	fcntl(STDIN_FILENO, F_SETFL, fcntl(socket_desc, F_GETFL) | O_NONBLOCK);

	// char buf[BUFSIZ];
	char buf[BUFSIZ];
	int active_file = 0;
	ssize_t len;
	int file_size = 0, remain_data = 0;
	FILE *received_file;
	char *time = NULL;
	int room = 0;
	int color = 0;
	while (1)
	{
		bzero(buf, BUFSIZ);

		//Receive a reply from the server
		if ((n = recv(socket_desc, buf, BUFSIZ, 0)) <= 0)
		{
			if (errno != EAGAIN)
				puts("recv failed");
			if (n == 0)
			{
				puts("Connexion lost");
				close(socket_desc);
				break;
			}
		}
		else
		{
			if (buf[0] == '!')
			{
				printf("hollo from commantde\n");
				char *msg, *name;
				parse_message(buf, &name, &msg);
				if (!strcmp(name, "!file"))
				{
					char *token = get_token(1, buf);
					if (!strcmp(token, "receive"))
					{
						char *filename = get_token(2, buf);
						token = get_token(3, buf);
						int filesize;
						if (sscanf(token, "%d", &filesize) == EOF)
						{
							printf("la taille du fichier doit être un entier\n");
							return;
						}
						printf("%s , %d", filename, filesize);
						if (fcntl(socket_desc, F_SETFL, 0) == -1)
							stop("fcntl()");
						int flags = fcntl(socket_desc, F_GETFL, 0);
						flags &= ~O_NONBLOCK;
						fcntl(socket_desc, F_SETFL, flags);

						remain_data = filesize;
						if (fcntl(socket_desc, F_GETFL) & O_NONBLOCK)
						{
							// socket is non-blocking
							printf("en non bloquant\n");
						}
						received_file = fopen(filename, "w+");
						char res[BUFSIZ];
						bzero(res, BUFSIZ);
						ssize_t len = 0;
						while ((remain_data > 0) && ((len = recv(socket_desc, res, BUFSIZ, 0)) > 0))
						{
							printf("fichier %d\n", len);
							fwrite(res, sizeof(char), len, received_file);

							// // 	// fwrite(buffer, sizeof(char), len, received_file);
							remain_data -= len;
							// fprintf(stdout, "Receive %d bytes and we hope :- %d bytes\n", len, remain);
							bzero(res, BUFSIZ);
						}
						fclose(received_file);

						// // printf("%s", buf);
						fcntl(socket_desc, F_SETFL, fcntl(socket_desc, F_GETFL) | O_NONBLOCK);
					}
				}
			}
			print_new_message(buf, &time);
			print_interface(room, pseudo, color);
		}

		// STDIN read
		bzero(buf, BUFSIZ);
		if (read(STDIN_FILENO, buf, BUFSIZ - 1) < 0)
		{
			if (errno != EAGAIN)
				puts("recv failed");
		}
		else
		{
			// TODO il faut analyser le texte pour détecter le changement de nom
			if (strcmp(buf, "\n"))
			{
				// Si on demande un fichier, alors il faut se mettre en mode while et on recoit en amont la taille du fichier
				// Il ne faut pas envoyer de message pour les commandes locales comme color
				if (buf[0] == '!')
				{
					char *msg, *name;
					parse_message(buf, &name, &msg);
					// TODO il faut regardr si on a le pseudo pour le changer
					if (!strcmp(name, "!file"))
					{
						char *token = get_token(1, buf);
						if (!strcmp(token, "send"))
						{
							char *filename = get_token(2, buf);
							filename[strlen(filename) - 1] = '\0';
							struct stat file_stat;
							int fd;
							char file_size[256];
							// Open the file
							fd = open(filename, O_RDONLY);
							if (fd == -1)
							{
								fprintf(stderr, "Error opening file --> %s\n", strerror(errno));
								exit(EXIT_FAILURE);
							}
							// Get the file state
							if (fstat(fd, &file_stat) < 0)
							{
								fprintf(stderr, "Error fstat --> %s\n", strerror(errno));
								exit(EXIT_FAILURE);
							}
							// Get the file size
							sprintf(file_size, "%d", file_stat.st_size);
							fprintf(stdout, "File Size: \n%s bytes\n", file_size);
							sprintf(buf, "!file send %s %s\n", filename, file_size);
							send(socket_desc, buf, BUFSIZ, 0);
							off_t offset = 0;
							int remain_data = file_stat.st_size;
							int sent_bytes = 0;
							/* Sending file data */
							while (((sent_bytes = sendfile(socket_desc, fd, &offset, BUFSIZ)) > 0) && (remain_data > 0))
							{
								fprintf(stdout, "1. Server sent %d bytes from file's data, offset is now : %d and remaining data = %d\n", sent_bytes, offset, remain_data);
								remain_data -= sent_bytes;
								fprintf(stdout, "2. Server sent %d bytes from file's data, offset is now : %d and remaining data = %d\n", sent_bytes, offset, remain_data);
							}
							print_interface(room, pseudo, color);
						}
						// // if (fcntl(socket_desc, F_SETFL, 0) == -1)
						// // 	stop("fcntl()");
						// int flags = fcntl(socket_desc, F_GETFL, 0);
						// flags &= ~O_NONBLOCK;
						// fcntl(socket_desc, F_SETFL, flags);

						// recv(socket_desc, buf, BUFSIZ, 0);
						// printf("%s", buf);
						// file_size = atoi(buf);
						// remain_data = file_size;
						// printf("file size %d\n", remain_data);
						// if (fcntl(socket_desc, F_GETFL) & O_NONBLOCK)
						// {
						// 	// socket is non-blocking
						// 	printf("en non bloquant\n");
						// }
						// while ((remain_data > 0) && ((len = recv(socket_desc, buf, BUFSIZ, 0)) > 0))
						// {
						// 	// printf("fichier %s\n", buf);
						// 	fwrite(buf, sizeof(char), len, received_file);

						// 	// 	// fwrite(buffer, sizeof(char), len, received_file);
						// 	remain_data -= len;
						// 	fprintf(stdout, "Receive %d bytes and we hope :- %d bytes\n", len, remain_data);
						// 	bzero(buf, BUFSIZ);
						// }
						// fclose(received_file);

						// // printf("%s", buf);
						// fcntl(socket_desc, F_SETFL, fcntl(socket_desc, F_GETFL) | O_NONBLOCK);
					} // Soucis avec la taille du message
					else if (!strcmp(name, "!room"))
					{
						char *token = get_token(1, buf);
						if (sscanf(token, "%d", &room) == EOF)
						{
							printf("le numéro de room doit être un entier\n");
						}
						else
						{
							print_interface(room, pseudo, color);
						}
					}
					else if (!strcmp(name, "!color"))
					{
						char *token = get_token(1, buf);
						if (sscanf(token, "%d", &color) == EOF)
						{
							printf("le numéro de room doit être un entier\n");
						}
						else
						{
							print_interface(room, pseudo, color);
						}
					}
				}
				send(socket_desc, buf, strlen(buf), 0);
			}
			// file = 1;
		}

		usleep(100000);
	}
	return 0;
}

/**
 * @brief Get the token in a string
 * 
 * @param number 
 * @param str 
 * @return char* 
 */
char *get_token(int number, char *str)
{
	char *token = NULL;
	char *buf_dup = strdup(str);
	for (int i = 0; i <= number; i++)
		token = strsep(&buf_dup, " ");
	// free(buf_dup);
	return token;
}

/**
 * @brief Print a new message
 * 
 * @param buf 
 */
void print_new_message(char *buf, char **time)
{
	char *name = NULL;
	char *msg = NULL;
	parse_message(buf, &name, &msg);
	if (!strcmp(name, "previous"))
	{
		printf("%sHistory%s\n", KYEL, KNRM);
		char buff[1024];
		bzero(buff, 1024);
		for (int i = 0; i < strlen(msg) - 1; i++)
		{
			if (msg[i] != '\n')
			{
				strncat(buff, msg + i, 1);
			}
			else
			{
				char *prev_name = NULL;
				char *prev_msg = NULL;
				parse_message(buff, &prev_name, &prev_msg);
				printf("%s%s%s say %s:\n  %s\n", KRED, prev_name, KNRM, KNRM, prev_msg);
				bzero(buff, 1024);
			}
		}
	}
	else
	{
		get_time(time);
		printf("At %s%s%s, %s%s%s say %s:\n  %s\n", KGRN, *time, KNRM, KRED, name, KNRM, KNRM, msg);
	}
}

/**
 * @brief Print the user info
 * 
 * @param pseudo 
 */
void print_interface(int room, char *pseudo, int color)
{
	// TODO il faut faire la couleur (mais je sèche sur le comment) (mettre les couleurs dans des variables)
	printf("%sRoom %d%s, %s%s%s :\n", KBLU, room, KNRM, colors[color], pseudo, KNRM);
}

/**
 * @brief Used to get the time using a NTP request
 * 
 * @param char**
 */
void get_time(char **str)
{
	int sockfd, n; // Socket file descriptor and the n return result from writing/reading from the socket.

	int portno = 123; // NTP UDP port number.

	char *host_name = "fr.pool.ntp.org"; // NTP server host-name.

	// Structure that defines the 48 byte NTP packet protocol.

	typedef struct
	{

		uint8_t li_vn_mode; // Eight bits. li, vn, and mode.
												// li.   Two bits.   Leap indicator.
												// vn.   Three bits. Version number of the protocol.
												// mode. Three bits. Client will pick mode 3 for client.

		uint8_t stratum;	 // Eight bits. Stratum level of the local clock.
		uint8_t poll;			 // Eight bits. Maximum interval between successive messages.
		uint8_t precision; // Eight bits. Precision of the local clock.

		uint32_t rootDelay;			 // 32 bits. Total round trip delay time.
		uint32_t rootDispersion; // 32 bits. Max error aloud from primary clock source.
		uint32_t refId;					 // 32 bits. Reference clock identifier.

		uint32_t refTm_s; // 32 bits. Reference time-stamp seconds.
		uint32_t refTm_f; // 32 bits. Reference time-stamp fraction of a second.

		uint32_t origTm_s; // 32 bits. Originate time-stamp seconds.
		uint32_t origTm_f; // 32 bits. Originate time-stamp fraction of a second.

		uint32_t rxTm_s; // 32 bits. Received time-stamp seconds.
		uint32_t rxTm_f; // 32 bits. Received time-stamp fraction of a second.

		uint32_t txTm_s; // 32 bits and the most important field the client cares about. Transmit time-stamp seconds.
		uint32_t txTm_f; // 32 bits. Transmit time-stamp fraction of a second.

	} ntp_packet; // Total: 384 bits or 48 bytes.

	// Create and zero out the packet. All 48 bytes worth.

	ntp_packet packet = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	memset(&packet, 0, sizeof(ntp_packet));

	// Set the first byte's bits to 00,011,011 for li = 0, vn = 3, and mode = 3. The rest will be left set to zero.

	*((char *)&packet + 0) = 0x1b; // Represents 27 in base 10 or 00011011 in base 2.

	// Create a UDP socket, convert the host-name to an IP address, set the port number,
	// connect to the server, send the packet, and then read in the return packet.

	struct sockaddr_in serv_addr; // Server address data structure.
	struct hostent *server;				// Server data structure.

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // Create a UDP socket.

	if (sockfd < 0)
		error("ERROR opening socket");

	server = gethostbyname(host_name); // Convert URL to IP.

	if (server == NULL)
		error("ERROR, no such host");

	// Zero out the server address structure.

	bzero((char *)&serv_addr, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;

	// Copy the server's IP address to the server address structure.

	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);

	// Convert the port number integer to network big-endian style and save it to the server address structure.

	serv_addr.sin_port = htons(portno);

	// Call up the server using its IP address and port number.

	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		error("ERROR connecting");

	// Send it the NTP packet it wants. If n == -1, it failed.

	n = write(sockfd, (char *)&packet, sizeof(ntp_packet));

	if (n < 0)
		error("ERROR writing to socket");

	// Wait and receive the packet back from the server. If n == -1, it failed.

	n = read(sockfd, (char *)&packet, sizeof(ntp_packet));

	if (n < 0)
		error("ERROR reading from socket");

	// These two fields contain the time-stamp seconds as the packet left the NTP server.
	// The number of seconds correspond to the seconds passed since 1900.
	// ntohl() converts the bit/byte order from the network's to host's "endianness".

	packet.txTm_s = ntohl(packet.txTm_s); // Time-stamp seconds.
	packet.txTm_f = ntohl(packet.txTm_f); // Time-stamp fraction of a second.

	// Extract the 32 bits that represent the time-stamp seconds (since NTP epoch) from when the packet left the server.
	// Subtract 70 years worth of seconds from the seconds since 1900.
	// This leaves the seconds since the UNIX epoch of 1970.
	// (1900)------------------(1970)**************************************(Time Packet Left the Server)

	time_t txTm = (time_t)(packet.txTm_s - NTP_TIMESTAMP_DELTA);
	struct tm *ptm = localtime(&txTm);
	// Print the time we got from the server, accounting for local timezone and conversion from UTC time.
	*str = malloc(strlen("[00:00]"));
	sprintf(*str, "[%02d:%02d]", ptm->tm_hour, ptm->tm_min);
}

/**
 * @brief Parse the buffer to get a name and a message
 * 
 * @param str 
 * @param name 
 * @param msg 
 * @return char* 
 */
void parse_message(char *str, char **name, char **msg)
{
	*msg = strdup(str);
	*name = strsep(msg, " ");
}