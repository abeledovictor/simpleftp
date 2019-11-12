#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "debug.h"

#define BUFSIZE 512
#define UINT16_MAX 65535
#define BACKLOG 10

#define MSG_550 "550 %s: no such file or directory\r\n"

int get_digits(char *string) {
    char *ptr = string;
    while (*ptr) {
        if (isdigit(*ptr)) {
            long val = strtol(ptr, &ptr, 10);
            return (int) val;
        } else {
            ptr++;
        }
    }
    return 0;
}

/**
 * function: receive and analize the answer from the server
 * sd: socket descriptor
 * code: three leter numerical code to check if received
 * text: normally NULL but if a pointer if received as parameter
 *       then a copy of the optional message from the response
 *       is copied
 * return: result of code checking
 **/
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // receive the answer
    recv_s = recv(sd, buffer, BUFSIZE, 0);
    DEBUG_PRINT(("recv_s: %d\n", recv_s));

    // error checking
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    printf("%d %s\n", recv_code, message);
    // optional copy of parameters
    if(text) strcpy(text, message);
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * function: send command formated to the server
 * sd: socket descriptor
 * operation: four letters command
 * param: command parameters
 **/
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";
    int bytes_sent;

    // command formating
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // send command and check for errors
    bytes_sent = send(sd, buffer, strlen(buffer), 0);
    if(bytes_sent == -1)
    {
        perror("send error: ");
    }
}

/**
 * function: simple input from keyboard
 * return: input without ENTER key
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

/**
 * function: login process from the client side
 * sd: socket descriptor
 **/
void authenticate(int sd) {
    char *input, desc[100];
    int code;

    // ask for user
    printf("username: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "USER", input);
    
    // relese memory
    free(input);

    // wait to receive password requirement and check for errors
    if(!recv_msg(sd, 331, desc))
    {
        warn("Password request not received from server\n");
    }
    printf("Server says: %s\n", desc);

    // ask for password
    printf("passwd: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "PASS", input);

    // release memory
    free(input);

    // wait for answer and process it and check for errors
    if(!recv_msg(sd, 230, desc))
    {
        warn("Authentication status not received or incorrect from server\n");
    }
    printf("Server says: %s\n", desc);

}

void put(int sd, int datasd, char *file_name) {
    char desc[BUFSIZE], buffer[BUFSIZE];
    int f_size, recv_s, r_size = BUFSIZE;
    int bread;
    FILE *file;

    if ((file = fopen(file_name, "r")) == NULL) {
        printf(MSG_550, file_name);
        return;
    }

    send_msg(sd, "STOR", file_name);
    
    recv_msg(sd, 200, NULL);

    recv_msg(sd, 150, NULL);
 
    while(1) {
        bread = fread(buffer, 1, BUFSIZE, file);
        if (bread > 0) {
            send(datasd, buffer, bread, 0);
            sleep(1);
        }
        if (bread < BUFSIZE) break;
    }
    
    fclose(file);

    recv_msg(sd, 226, NULL);
}

/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 **/
void get(int sd, int server_sd, char *file_name) {
    char desc[BUFSIZE], buffer[BUFSIZE];
    int f_size, recv_s, r_size = BUFSIZE;
    FILE *file;
    ssize_t bytes_read;
    ssize_t escritos = 0;
    
    // send the RETR command to the server
    send_msg(sd,"RETR",file_name);
    // check for the response
    if(recv_msg(sd,150,buffer)){
        // parsing the file size from the answer received
        f_size = get_digits(buffer);
        
        // open the file to write
        file = fopen(file_name, "w"); 
              
        while(escritos < f_size){
            bytes_read = read(server_sd,desc,BUFSIZE);
            escritos = escritos + bytes_read;
            fwrite(desc, sizeof(char), bytes_read, file);
        }
        
        // close the file
        fclose(file);
        close(server_sd);

        // receive the OK from the server
        recv_msg(sd,226,NULL);
    }
}

/**
 * function: operation quit
 * sd: socket descriptor
 **/
void quit(int sd) {
    // send command QUIT to the client
    send_msg(sd, "QUIT", NULL);
    // receive the answer from the server
    recv_msg(sd, 221, NULL);
}

/**
 * function: make all operations (get|quit|put)
 * sd: socket descriptor
 **/
void operate(int sd, int server_sd) {
    char *input, *op, *param;

    while(true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        // free(input);
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, server_sd, param);
        }
        else if(strcmp(op, "quit") == 0) {
            close(server_sd);
            quit(sd);
            break;
        }
        else if(strcmp(op, "put") == 0) {
            param = strtok(NULL, " ");
            put(sd, server_sd, param);
        }
        else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
        free(input);
    }
    free(input);
}

bool isValidIpAddress(char *ipAddress)
{
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
    return result != 0;
}

bool isValidPortNumber(char *portNumber)
{
    int port = atoi(portNumber);
    return (port < UINT16_MAX && port > 0);
}

unsigned int convert(char *st) {
  char *x;
  for (x = st ; *x ; x++) {
    // if its not a valid string (only digits), just return 0
    if (!isdigit(*x))
      return 0L;
  }
  return (strtoul(st, 0L, 10));
}

void convertPort(uint16_t port, int *n5, int *n6) {
    int i = 0;
    int x = 1;
    *n5 = 0;
    *n6 = 0;
    int temp = 0;
    for(i = 0; i < 8; i++) {
        temp = port & x;
        *n6 = (*n6)|(temp);
        x = x << 1; 
    }

    port = port >> 8;
    x = 1;

    for(i = 8; i < 16; i++){
        temp = port & x;
        *n5 = ((*n5)|(temp));
        x = x << 1; 
    }
}

void getIpPort(int sd, char *ip, int *port) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    getsockname(sd, (struct sockaddr*) &addr, &len);

    sprintf(ip,"%s", inet_ntoa(addr.sin_addr));

    *port = (uint16_t)ntohs(addr.sin_port);
}

void getPortString(char *str, char *ip, int n5, int n6) {
    int i = 0;
    char ip_temp[1024];
    strcpy(ip_temp, ip);

    for(i = 0; i < strlen(ip); i++){
        if(ip_temp[i] == '.'){
            ip_temp[i] = ',';
        }
    }
    sprintf(str, "%s,%d,%d", ip_temp, n5, n6);
}

/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    int sd, data_transfer_sd, srvr_data_transfer_sd;
    int port, data_transfer_port;
    int n5, n6;
    struct sockaddr_in addr, data_transfer_addr;
    char *server_ipaddr = argv[1];
    char *server_portn = argv[2];
    char data_transfer_ip[BUFSIZE+1];
    char ip[50];

    // arguments checking
    if(argc == 3 && isValidPortNumber(server_portn))
    {
        printf("arguments are valid :D\n");
    } else
    {
        DEBUG_PRINT(("args: %d\nserverIp: %s\nserverPort: %s\n", argc, server_ipaddr, server_portn));
        printf("Wrong numer of arguments or argument formatting\nExpecting: ./myftp x.x.x.x 2280\nWhere the first arg is the server ip addr and the second one is the server port\n");
        exit(EXIT_FAILURE);
    }

    //....
    struct hostent *host;
    
    if ((host = gethostbyname(server_ipaddr)) == NULL) {   
        printf("There is not a server available at %s\n", server_ipaddr);
        exit(EXIT_FAILURE);
    }
    //.....
    // create socket and check for errors
    sd = socket(AF_INET, SOCK_STREAM, 0);
    DEBUG_PRINT(("socket descriptor [sd]: %d\n", sd));
    if(sd == -1)
    {
        printf("Error opening socket\n");
        exit(EXIT_FAILURE);
    }

    // set socket data 
    addr.sin_family = AF_INET;
    addr.sin_port = htons(convert(server_portn));
    DEBUG_PRINT(("addr.sin_port (without using htons): %u\n", convert(server_portn)));
    DEBUG_PRINT(("addr.sin_port: %u\n", addr.sin_port));
    addr.sin_addr = *((struct in_addr *)host->h_addr);
    memset(&(addr.sin_zero), '\0', 8); 

    // connect and check for errors
    // remember to use ports > 1024 if not, a binding error is likely to happen
    // binding error == port in use
    if(connect(sd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        printf("Connection with the server failed...\n");
        exit(EXIT_FAILURE);
    }

    printf("Successfully connected to server %s\n", server_portn);   

    // if receive hello proceed with authenticate and operate if not warning
    if(!recv_msg(sd, 220, NULL))
    {
        warn("Hello message not received from server");
    }
    authenticate(sd);
    // set up connection for data transfer
    data_transfer_sd = socket(AF_INET, SOCK_STREAM, 0);

    data_transfer_addr.sin_family = AF_INET;
    data_transfer_addr.sin_port = htons(0);
    data_transfer_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(&(data_transfer_addr.sin_zero), '\0', 8);

    bind(data_transfer_sd, (struct sockaddr*) &data_transfer_addr, sizeof(data_transfer_addr));
    listen(data_transfer_sd, BACKLOG);

    getIpPort(sd, ip, (int *) &port);
    getIpPort(data_transfer_sd, data_transfer_ip, (int *) &data_transfer_port);

    convertPort(data_transfer_port, &n5, &n6);
    getPortString(data_transfer_ip, ip, n5, n6);
    DEBUG_PRINT(("data_transfer_ip %s \n", data_transfer_ip));

    send_msg(sd, "PORT", data_transfer_ip);
    bzero(data_transfer_ip, (int)sizeof(data_transfer_ip));

    srvr_data_transfer_sd = accept(data_transfer_sd, (struct sockaddr*) NULL, NULL);

    DEBUG_PRINT(("srvr_data_transfer_sd %d \n", srvr_data_transfer_sd));
    operate(sd, srvr_data_transfer_sd);

    // close sockets
    close(srvr_data_transfer_sd);
    close(sd);

    return 0;
}
