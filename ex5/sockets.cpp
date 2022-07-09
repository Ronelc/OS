#include <netinet/in.h>
#include <armadillo>
#include <netinet/in.h>
#include <netdb.h>
#define MAX_HOSTNAME 256
#define BUFF_SIZE 256
#define MAX_CLIENTS 5

using namespace std;


/**
 * open a new server
 * @param max_clients - number of clients in wait mood
 * @return socket number if success, -1 otherwise
 */
int open_server (int max_clients, unsigned short port_num) {

  /* init struct params */
  struct sockaddr_in sock_struct; // init struct
  char my_name[MAX_HOSTNAME + 1];
  struct hostent *hp;
  gethostname (my_name, MAX_HOSTNAME);
  hp = gethostbyname (my_name);
  if (hp == nullptr) { return -1; }
  memset (&sock_struct, 0, sizeof (sockaddr_in));
  sock_struct.sin_family = hp->h_addrtype;
  memcpy (&sock_struct.sin_addr, hp->h_addr, hp->h_length);
  sock_struct.sin_port = htons (port_num);

  /* create and connect */
  int sockfd = socket (AF_INET, SOCK_STREAM, 0); // create sockets
  if (sockfd < 0) { return -1; }
  int bind_ret = bind (sockfd, (struct sockaddr *) &sock_struct, sizeof (sockaddr_in)); // connect sockets
  if (bind_ret < 0) {
    close (sockfd);
    return -1;
  }
  listen (sockfd, max_clients); //define max number of client
  return sockfd;
}

/**
 * read data from client
 * @param sockfd socket number
 * @param buff_size - size of buffer
 * @param buf  - buffer for read to it
 * @return positive number if success, -1 otherwise.
 */
int read_data (int sockfd, int buff_size, char *buf) {
  int bcount = 0; /* counts bytes read */
  int br = 0;     /* bytes read this pass */

  /* read data */
  while (bcount < buff_size) { /* loop until full buffer */
    br = read (sockfd, buf, buff_size - bcount);
    if (br > 0) {
      bcount += br;
      buf += br;
    }
    if (br < 1) { return -1; }
  }
  return bcount;
}

/**
 * create private socket for new client
 * @param sockfd - number of server socket
 * @return socket number if success, -1 otherwise
 */
int connect_server (int sockfd) {
  auto socket_for_client = accept (sockfd, nullptr, nullptr); // wait for clients
  if (socket_for_client < 0) { return -1; }
  return socket_for_client;
}

/**
 * function that run client
 * @param host_name - host name
 * @param port_num - port to connect
 * @return socket number if success, -1 otherwise
 */
int run_client (char *host_name, unsigned short port_num) {

  /* init struct params */
  struct sockaddr_in sock_struct; // init struct
  struct hostent *hp;
  hp = gethostbyname (host_name);
  if (hp == nullptr) { return -1; }
  memset (&sock_struct, 0, sizeof (sock_struct));
  memcpy ((char *) &sock_struct.sin_addr, hp->h_addr, hp->h_length);
  sock_struct.sin_family = hp->h_addrtype;
  sock_struct.sin_port = htons ((u_short) port_num);

  /* create sockets */
  int sockfd = socket (hp->h_addrtype, SOCK_STREAM, 0); // create sockets
  if (sockfd < 0) { return -1; }

  /* connect to server */
  int connect_ret = connect (sockfd, (struct sockaddr *) &sock_struct, sizeof (sock_struct)); // connect to the server
  if (connect_ret < 0) {
    close (sockfd);
    return -1;
  }
  return sockfd;
}

void error_handle(int val, int comp, string error_msg){
  if (val == comp){
    cerr << "system error: " << error_msg << endl;
  }
}

/**
 * main function
 * @param argc number of arguments
 * @param argv arguments
 * @return 1 if success, exit(1) otherwise
 */
int main (int argc, char *argv[]) {
  unsigned short port_num = atoi (argv[2]);
  if (strcmp ("server", argv[1])
      == 0) { // server case - run received terminal commands from port.
    int sockfd = open_server (MAX_CLIENTS, port_num);
    error_handle (sockfd, -1, "open server failed");
    while (true) {
      int socket_for_client = connect_server (sockfd);
      error_handle (socket_for_client, -1, "connect server failed");
      char *buff = static_cast<char *>(malloc (BUFF_SIZE));
      memset (buff, 0, BUFF_SIZE);
      int read = read_data (socket_for_client, BUFF_SIZE, buff);
      error_handle (read, -1, "read function failed");
      int s = system (buff);
      if (s != 0) {
        cerr << "system error: system function failed\n";
        exit (1);
      }
      free (buff);
    }
  }
  else if (strcmp ("client", argv[1])
           == 0) { // client case - send terminal command to server.
    char host_name[MAX_HOSTNAME + 1];
    gethostname (host_name, MAX_HOSTNAME);
    int sockfd = run_client (host_name, port_num);
    error_handle (sockfd, -1, "run client failed");
    string com;
    for (int i = 3; i < argc; i++) {
      com += string (argv[i]) + " ";
    }
    int write = write (sockfd, com.c_str (), BUFF_SIZE);
    error_handle (write, -1, "write function failed");
  }
  return 1;
}