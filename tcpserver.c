/**
	Handle multiple socket connections with select and fd_set on Linux
*/

#include <stdio.h>
#include <string.h> //strlen
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>    //close
#include <arpa/inet.h> //close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h> //FD_SET, FD_ISSET, FD_ZERO macros
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>

#define TRUE 1
#define FALSE 0
#define PORT 8888
#define BUFSIZE 1025

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

void stop(char *msg)
{
  perror(msg);
  exit(EXIT_FAILURE);
}

struct client_info
{
  int socket;
  char *pseudo;
  int room;
};

void send_to_all(char *, int, int, struct client_info *);
char *get_token(int, char *);
void manage_commands(char *, int, int, struct client_info *);
void send_message(int fd, char *msg);
int get_number_line(FILE *file);
void error(char *str);
void save_message(char *path, char *str);
int find_user_by_index(int, int, struct client_info *, char *);
// TODO pouvoir changer le pseudo et utiliser une commande pour change le nom dans le serveur (une fois que l'utilisation des commandes est mise en place, alors on est bon pour intégrer plein de truc mais donc il faut que ça soit prêt (ça veut dire couper les string)

int main(int argc, char *argv[])
{
  int opt = TRUE;
  int master_socket, addrlen, new_socket, client_socket[30], max_clients = 30, activity, i, valread, sd;
  int max_sd;
  struct client_info clients[30];
  FILE *file;
  for (int i = 0; i < max_clients; i++) // TODO voir pour le bzero
  {
    clients[i].socket = 0;
    clients[i].room = 0;
    clients[i].pseudo = NULL;
  }
  struct sockaddr_in address;

  char buffer[BUFSIZE]; //data buffer of 1K

  int fd;
  char *path = "chat.log";

  // fd = open(path, O_WRONLY | O_CREAT, 0666);
  // if (fd == -1)
  //   stop("open()");

  //set of socket descriptors
  fd_set readfds;

  //initialise all client_socket[] to 0 so not checked
  for (i = 0; i < max_clients; i++)
  {
    client_socket[i] = 0;
  }

  //create a master socket
  if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
  {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  //set master socket to allow multiple connections , this is just a good habit, it will work without this
  if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
  {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  //type of socket created
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  //bind the socket to localhost port 8888
  if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
  {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }
  char *addr = inet_ntoa(address.sin_addr);
  printf("%sListener on %s:%d %s\n", KGRN, addr, PORT, KNRM);

  //try to specify maximum of 3 pending connections for the master socket
  if (listen(master_socket, 3) < 0)
  {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  //accept the incoming connection
  addrlen = sizeof(address);
  printf("%sWaiting for connections ...%s\n\n", KYEL, KNRM);

  while (TRUE)
  {
    //clear the socket set
    FD_ZERO(&readfds);

    //add master socket to set
    FD_SET(master_socket, &readfds);
    max_sd = master_socket;

    //add child sockets to set
    for (i = 0; i < max_clients; i++)
    {
      //socket descriptor
      sd = client_socket[i];

      //if valid socket descriptor then add to read list
      if (sd > 0)
        FD_SET(sd, &readfds);

      //highest file descriptor number, need it for the select function
      if (sd > max_sd)
        max_sd = sd;
    }

    //wait for an activity on one of the sockets , timeout is NULL , so wait indefinitely
    activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

    if ((activity < 0) && (errno != EINTR))
    {
      printf("select error");
    }

    //If something happened on the master socket , then its an incoming connection
    if (FD_ISSET(master_socket, &readfds))
    {
      if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) // FIX lorsque il y a 3 connexions (dont une qui s'est remove)
      {
        perror("accept");
        exit(EXIT_FAILURE);
      }

      //inform user of socket number - used in send and receive commands
      printf("%s-----%s\n", KGRN, KNRM);
      printf("%sNew connection : %s:%d%s\n", KGRN, inet_ntoa(address.sin_addr), ntohs(address.sin_port), KNRM);

      // réception du pseudo
      int n = 0;
      char buffer[BUFSIZE];
      if ((n = recv(new_socket, buffer, BUFSIZE, 0)) == -1)
      {
        error("recv()");
      }
      else if (n > 0)
      {

        buffer[n] = '\0'; // TODO vérifier que le pseudo et inconnue
        printf("%sWelcome '%s' to the server%s\n", KGRN, buffer, KNRM);

        for (i = 0; i < max_clients; i++)
        {
          if (clients[i].socket != 0 && !strcmp(clients[i].pseudo, buffer))
          {
            char *msg = "server This pseudo is already given";
            send_message(new_socket, msg);
            close(new_socket);
            new_socket = 0;
            break;
          }
        }

        if (new_socket != 0)
        {
          //add new socket to array of sockets
          send_history(new_socket, path);
          for (i = 0; i < max_clients; i++)
          {
            if (clients[i].socket == 0)
            {
              client_socket[i] = new_socket;
              clients[i].pseudo = malloc(sizeof(buffer));
              strcpy(clients[i].pseudo, buffer);
              clients[i].socket = new_socket;
              printf("%sAdding '%s' to list of sockets as %d%s\n\n", KGRN, clients[i].pseudo, i, KNRM);
              char *msg = " is coming in the server\n";
              char *res = malloc(strlen(clients[i].pseudo) + strlen(msg));
              sprintf(res, "%s%s", clients[i].pseudo, msg);
              send_to_all(res, i, max_clients, clients);
              free(res);
              break;
            }
          }
        }
      }
    }

    //else its some IO operation on some other socket :)
    for (i = 0; i < max_clients; i++)
    {
      sd = clients[i].socket;

      if (FD_ISSET(sd, &readfds))
      {
        //Check if it was for closing , and also read the incoming message
        if ((valread = read(sd, buffer, 1024)) == 0)
        {
          //Somebody disconnected , get his details and print
          getpeername(sd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
          printf("%sHost disconnected : %s:%d%s\n", KYEL, inet_ntoa(address.sin_addr), ntohs(address.sin_port), KNRM);

          //Close the socket and mark as 0 in list for reuse
          close(sd);
          clients[i].socket = 0;
          clients[i].room = 0;
          clients[i].pseudo = NULL;
        }
        else
        {
          //set the string terminating NULL byte on the end of the data read
          buffer[valread - 1] = '\0'; // -1 to remove le \n
          printf("%s\n", buffer);
          if (buffer[0] == '!')
          {
            manage_commands(buffer, i, max_clients, clients);
          }
          else
          {
            // on gère l'envoie des messages
            // création du message
            int len = strlen(clients[i].pseudo) + strlen(buffer) + strlen(" ") + 2;
            char *msg = malloc(len);
            if (msg == NULL)
            {
              perror("Unable to allocate memory\n");
              exit(1);
            }
            sprintf(msg, "%s %s", clients[i].pseudo, buffer);
            // envoie du message à tous les clients
            send_to_all(msg, i, max_clients, clients);
            sprintf(msg, "%s %s\n", clients[i].pseudo, buffer);
            save_message(path, msg);
          }
        }
      }
    }
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
 * @brief Used to send a message to all users
 * 
 * @param msg 
 * @param current_client 
 * @param max_client 
 * @param clients 
 */
void send_to_all(char *msg, int current_client, int max_client, struct client_info *clients)
{
  for (int j = 0; j < max_client; j++)
  {
    for (j = 0; j < max_client; j++)
    {
      if (j != current_client && clients[j].socket > 0 && clients[j].room == clients[current_client].room)
      {
        // i est le client qui envoie le message
        if ((send(clients[j].socket, msg, strlen(msg), 0)) < 0)
        {
          printf("Sending error !\n");
        }
      }
    }
  }
}

/**
 * @brief Used to manage all commands
 * 
 * @param buffer 
 */
void manage_commands(char *buffer, int current_client, int max_client, struct client_info *clients)
{
  // récupération du token
  char *token = get_token(0, buffer);
  char *msg = NULL;
  // gestion des commandes
  if (!strcmp(token, "!help"))
  {
    token = get_token(1, buffer);
    if (token == NULL)
    {
      printf("on doit renvoyer toutes les commandes à l'utilisateur\n");
    }
    printf("le client demande de l'aide, %s\n", get_token(1, buffer));
  }
  else if (!strcmp(token, "!pseudo"))
  {
    token = get_token(1, buffer);
    if (token == NULL)
    {
      msg = "New pseudo is missing\n";
    }
    else
    {
      msg = "New pseudo is se\nt";
      // clients[current_client].pseudo = realloc(&(clients[current_client].pseudo), strlen(token) + 1);
      clients[current_client].pseudo = malloc(strlen(token));
      strcpy(clients[current_client].pseudo, token);
    }
  }
  else if (!strcmp(token, "!room"))
  {
    int room;
    token = get_token(1, buffer);
    if (token == NULL)
      return;
    if (sscanf(token, "%d", &room) == EOF)
      return;

    clients[current_client].room = room;
    char *msg = malloc(sizeof(char) * 256);
    sprintf(msg, "%s is coming in the room %d\n", clients[current_client].pseudo, clients[current_client].room);
    send_to_all(msg, current_client, max_client, clients);
    free(msg);
  }
  else if (!strcmp(token, "!mp"))
  {
    token = get_token(1, buffer);
    if (token == NULL)
      return;

    printf("%s\n", token);
    printf("%d\n", find_user_by_index(max_client, current_client, clients, token));
    int user_id;
    if ((user_id = find_user_by_index(max_client, current_client, clients, token)) != -1)
    {
      char *msg = malloc(sizeof(char) * 256);
      char *buf_dup = strdup(buffer);
      strsep(&buf_dup, " ");
      strsep(&buf_dup, " "); // On duplique pour ne garder que le message, on enlève la commande et le nom
      sprintf(msg, "%s (MP) %s", clients[current_client].pseudo, buf_dup);
      send_message(clients[user_id].socket, msg);
      free(msg);
      // free(buf_dup);
    }
  }
  else if (!strcmp(token, "!wiz"))
  {
    token = get_token(1, buffer);
    if (token == NULL)
      return;

    for (int i = 0; i < max_client; i++)
    {
      if (i != current_client)
      {
        if (clients[i].pseudo != NULL && !strcmp(token, clients[i].pseudo))
        {
          char *msg = malloc(sizeof(char) * 256);
          sprintf(msg, "server %s vous a envoyé un wiz en room %d", clients[current_client].pseudo, clients[current_client].room);
          send_message(clients[i].socket, msg);
          free(msg);
        }
      }
    }
  }
  else if (!strcmp(token, "!file"))
  {
    token = get_token(1, buffer); // file to load
    if (!strcmp(token, "send"))
    {
      char *filename = get_token(2, buffer);
      token = get_token(3, buffer);
      char *username = get_token(4, buffer);
      username[strlen(username) - 1] = '\0';

      int filesize = 0;
      if (sscanf(token, "%d", &filesize) == EOF)
      {
        printf("la taille du fichier doit être un entier\n");
      }
      // FILE *file;
      // file = fopen(filename, "w+");
      int user_id = find_user_by_index(max_client, current_client, clients, username);
      char *res[256];
      sprintf(res, "!file receive %s %d", filename, filesize);
      send_message(clients[user_id].socket, res);
      printf("%s, %d\n", username, filesize);
      if (user_id == -1)
        return;
      int remain_data = filesize;
      ssize_t len;
      char req[BUFSIZ];
      bzero(req, BUFSIZ);
      while ((remain_data > 0) && ((len = recv(clients[current_client].socket, req, BUFSIZ, 0)) > 0))
      {
        printf("fichier %s\n", req);
        // printf("%d\n", clients[user_id].socket, );
        // send_message(clients[user_id].socket, buffer);
        // 	// fwrite(buffer, sizeof(char), len, file);
        remain_data -= len;
        fprintf(stdout, "Receive %d bytes and we hope :- %d bytes\n", len, remain_data);
        bzero(req, BUFSIZ);
      }
      // fclose(file);
    }

    // printf("client wants a file\n");
    // token = get_token(1, buffer); // file to load
    // struct stat file_stat;
    // int fd;
    // char file_size[256];
    // fd = open(token, O_RDONLY);
    // if (fd == -1)
    // {
    //   fprintf(stderr, "Error opening file --> %s", strerror(errno));

    //   exit(EXIT_FAILURE);
    // }
    // /* Get file stats */
    // if (fstat(fd, &file_stat) < 0)
    // {
    //   fprintf(stderr, "Error fstat --> %s", strerror(errno));

    //   exit(EXIT_FAILURE);
    // }
    // sprintf(file_size, "%d", file_stat.st_size);
    // fprintf(stdout, "File Size: \n%d bytes\n", file_stat.st_size);

    // /* Sending file size */
    // ssize_t len = send(clients[current_client].socket, file_size, sizeof(file_size), 0);
    // if (len < 0)
    // {
    //   fprintf(stderr, "Error on sending greetings --> %s", strerror(errno));

    //   exit(EXIT_FAILURE);
    // }
    // off_t offset = 0;
    // int remain_data = file_stat.st_size;
    // int sent_bytes = 0;
    // /* Sending file data */
    // while (((sent_bytes = sendfile(clients[current_client].socket, fd, &offset, BUFSIZ)) > 0) && (remain_data > 0))
    // {
    //   fprintf(stdout, "1. Server sent %d bytes from file's data, offset is now : %d and remaining data = %d\n", sent_bytes, offset, remain_data);
    //   remain_data -= sent_bytes;
    //   fprintf(stdout, "2. Server sent %d bytes from file's data, offset is now : %d and remaining data = %d\n", sent_bytes, offset, remain_data);
    // }
  }
  else
  {
    msg = "No command found\n";
  }
  // printf("%s\n", msg);
  // free(token);
}

/**
 * @brief Permet d'envoyer des messages avec gestion des erreurs
 * 
 * @param fd 
 * @param msg 
 */
void send_message(int fd, char *msg)
{
  if (send(fd, msg, strlen(msg), 0) < 0)
    error("send()");
}

/**
 * @brief Get the number of line of a file.
 * 
 * @param file 
 * @return int 
 */
int get_number_line(FILE *file)
{
  int count = 0;
  char *ch;
  while ((ch = fgetc(file)) != EOF)
  {
    if (ch == '\n')
    {
      count++;
    }
  }
  return count;
}

void send_previous_message(int fd, int number, int linesCount, FILE *file)
{
  char buff[2048];
  rewind(file);
  send_message(fd, "previous ");
  for (int i = 0; i < linesCount; i++)
  {
    fscanf(file, "%[^\n]\n", buff);
    if (linesCount > 0 && i > linesCount - number + 1) // le -1 permet de ne pas prendre en compte la dernière ligne vide
    {
      send_message(fd, buff);
      send_message(fd, "\n");
    }
  }
  send_message(fd, "\n");
}

/**
 * @brief Used to send the history to the client
 * 
 * @param socket 
 * @param path 
 */
void send_history(int socket, char *path)
{
  FILE *file = fopen(path, "a+");
  if (file == NULL)
    error("fopen()");
  int linesCount = get_number_line(file);
  send_previous_message(socket, 10, linesCount, file);
  fclose(file);
}

/**
 * @brief Save a message in a file
 * 
 * @param path 
 * @param str 
 */
void save_message(char *path, char *str)
{
  FILE *file = fopen(path, "a+");
  if (file == NULL)
    stop("fopen()");
  fprintf(file, "%s", str);
  fclose(file);
}

/**
 * @brief Used to find the index of a user
 * 
 * @param max_client 
 * @param current_client 
 * @param clients 
 * @param username
 * @return int -1 si aucun personne n'est trouvé
 */
int find_user_by_index(int max_client, int current_client, struct client_info *clients, char *username)
{
  for (int i = 0; i < max_client; i++)
  {
    if (i != current_client)
    {
      if (clients[i].pseudo != NULL && !strcmp(username, clients[i].pseudo))
      {
        return i;
      }
    }
  }
  return -1;
}

/**
 * @brief Used to print color for error
 * 
 * @param str 
 */
void error(char *str)
{
  char *error = malloc(strlen(str) + strlen(KRED) + strlen(KNRM));
  sprintf(error, "%s%s%s", KRED, str, KNRM);
  perror(error);
  free(error);
}