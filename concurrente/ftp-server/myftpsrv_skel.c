#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <err.h>
#include <ctype.h>
#include <signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include "debug.h"

#define BUFSIZE 512
#define CMDSIZE 5
#define PARSIZE 100
#define UINT16_MAX 65535

#define MSG_150(OPTION) ((OPTION != 0) ? ("150 Opening BINARY mode data connection for %s (%ld bytes)\r\n"):("150 Opening BINARY mode data connection for %s\r\n"))
#define MSG_200 "200 PORT command successful\r\n"
#define MSG_220 "220 srvFtp version 1.0\r\n"
#define MSG_331 "331 Password required for %s\r\n"
#define MSG_230 "230 User %s logged in\r\n"
#define MSG_530 "530 Login incorrect\r\n"
#define MSG_221 "221 Goodbye\r\n"
#define MSG_550 "550 %s: no such file or directory\r\n"
#define MSG_299 "299 File %s size %ld bytes\r\n"
#define MSG_226 "226 Transfer complete\r\n"

/**
 * function: receive the commands from the client
 * sd: socket descriptor
 * operation: \0 if you want to know the operation received
 *            OP if you want to check an especific operation
 *            ex: recv_cmd(sd, "USER", param)
 * param: parameters for the operation involve
 * return: only usefull if you want to check an operation
 *         ex: for login you need the seq USER PASS
 *             you can check if you receive first USER
 *             and then check if you receive PASS
 **/
bool recv_cmd(int sd, char *operation, char *param) {
    char buffer[BUFSIZE], *token;
    int recv_s;

    // receive the command in the buffer and check for errors
    DEBUG_PRINT(("recv_cmd sd: %d operation %s param %s\n",sd,operation,param));
    recv_s = recv(sd, buffer, BUFSIZE, 0);
    DEBUG_PRINT(("after recv at recv_cmd: buffer: %s\nrecv_s: %d\n",buffer,recv_s));
    if (recv_s < 0)
    {
        warn("error receiving data");
        return false;
    }
    if (recv_s == 0) {
        warn("connection closed by client");
        return false;
    }

    // expunge the terminator characters from the buffer
    buffer[strcspn(buffer, "\r\n")] = 0;
    DEBUG_PRINT(("recv_cmd expunged buffer: %s\n",buffer));

    // complex parsing of the buffer
    // extract command receive in operation if not set \0
    // extract parameters of the operation in param if it needed
    token = strtok(buffer, " ");
    DEBUG_PRINT(("AFTER STRTOK token: %s\n", token));
    if (token == NULL || strlen(token) < 4) {
        warn("not valid ftp command");
        return false;
    } else {
        DEBUG_PRINT(("BEFORE STRcpy, operation: %s\n", operation));
        if (operation[0] == '\0') strcpy(operation, token);
        DEBUG_PRINT(("AFTER STRcpy, operation: %s\n", operation));
        if (strcmp(operation, token)) {
            warn("abnormal client flow: did not send %s command", operation);
            return false;
        }
        token = strtok(NULL, " ");
        DEBUG_PRINT(("AFTER token = strtok(NULL, \" \")\n"));
        if (token != NULL) strcpy(param, token);
        DEBUG_PRINT(("End of recv_cmd token: %s\n",token));
    }
    return true;
}

/**
 * function: send answer to the client
 * sd: file descriptor
 * message: formatting string in printf format
 * ...: variable arguments for economics of formats
 * return: true if not problem arise or else
 * notes: the MSG_x have preformated for these use
 **/
bool send_ans(int sd, char *message, ...) {
    char buffer[BUFSIZE];
    int bytes_sent;

    va_list args;
    va_start(args, message);

    vsprintf(buffer, message, args);
    va_end(args);
    // send answer preformated and check errors
    bytes_sent = send(sd, buffer, strlen(buffer), 0);
    if(bytes_sent == -1)
    {
        warn("Error sending to sd: %d message: %s\n", sd, buffer);
        return false;
    }
    return true;
}

/**
 * function: RETR operation
 * sd: socket descriptor
 * file_path: name of the RETR file
 **/
void retr(int sd, int transfersd, char *file_path) {
    FILE *file;
    int bread;
    long fsize;
    char buffer[BUFSIZE];

    // check if file exists if not inform error to client
    file = fopen(file_path, "r");
    if(access( file_path, F_OK ) == -1) {
        DEBUG_PRINT(("%s file doesnt exist\n", file_path));
        send_ans(sd, MSG_550, file_path);
        return;
    }

    // send success message with PORT OK
    //send_ans(sd, MSG_200);

    // send a success message with the file length
    file = fopen(file_path, "r") ;
    fseek(file, 0L, SEEK_END);
    fsize = ftell(file);
    rewind(file);
    DEBUG_PRINT(("FILE PATH: %s FILE_SIZE: %ld\n", file_path, fsize));
    send_ans(sd, MSG_150(fsize), file_path, fsize);

    // important delay for avoid problems with buffer size
    sleep(1);

    // send the file
        while((bread = fread(buffer,sizeof(char),BUFSIZE,file)) > 0){ 
            write(transfersd,buffer,bread);            
        }

    // close the file
    sleep(1);
    fclose(file);

    // send a completed transfer message
    send_ans(sd, MSG_226);
    close(transfersd);
}


void stor(int sd, int dsd, char *file_path) {
    FILE *file;    
    int recv_s;
    long fsize;
    int r_size = BUFSIZE;
    char desc[BUFSIZE];
    char buffer[BUFSIZE];

    // send a success message
    send_ans(sd, MSG_200);     
    
    // send a success message
    send_ans(sd, MSG_150(0), file_path);
    
    // open the file to write
    file = fopen(file_path, "w");
    
    // receive the file
    while(1) {
        recv_s = read(dsd, desc, r_size);
        if (recv_s > 0) fwrite(desc, 1, recv_s, file);
        if (recv_s < r_size){
            break;  
        } 
    }

    // close the file
    fclose(file);

    // send a completed transfer message
    send_ans(sd, MSG_226);
}

/**
 * funcion: check valid credentials in ftpusers file
 * user: login user name
 * pass: user password
 * return: true if found or false if not
 **/
bool check_credentials(char *user, char *pass) {
    FILE *file;
    char *path = "./ftpusers", *line = NULL, *cred;
    size_t len = 0;
    bool found = false;

    // make the credential string
    //asprintf(&cred,"%s:%s",user,pass);
    cred = malloc(strlen(user) + strlen(":") + strlen(pass) + 1); // +1 for the null-terminator
    strcpy(cred, user);
    strcat(cred, ":");
    strcat(cred, pass);
    DEBUG_PRINT(("CREDENTIALS: %s\n",cred));

    // check if ftpusers file it's present
    if(access( path, F_OK ) == -1) {
        DEBUG_PRINT(("./ftpusers file doesnt exist\n"));
        free(cred);
        return false;
    }

    // search for credential string
    line = (char*)malloc(1024) ;
    FILE* fp = fopen(path, "r") ;
    while (fgets(line , (strlen(cred) + 1) , fp )!= NULL)
    {
        DEBUG_PRINT(("searching: %s\n",line));
        if (strstr(line , cred )!= NULL)
        {
            found = true;
            break;
        }
    }
    // close file and release any pointes if necessary
    fclose(fp);
    free(line);
    free(cred);
    // return search status
    return found;
}

/**
 * function: login process management
 * sd: socket descriptor
 * return: true if login is succesfully, false if not
 **/
bool authenticate(int sd) {
    int bytes_sent;
    char user[PARSIZE], pass[PARSIZE];

    // wait to receive USER action
    if(!recv_cmd(sd, "USER", user))
    {
        warn("Error on user authentication [username]: %s\n", user);
        return false;
    }
    // ask for password
    if(!send_ans(sd, MSG_331, user))
    {
        return false;
    }

    // wait to receive PASS action
    if(!recv_cmd(sd, "PASS", pass))
    {
        warn("Error on user authentication [password]: %s\n", pass);
        return false;
    }
    DEBUG_PRINT(("PASSWORD RECEIVED: %s\n", pass));
    // if credentials don't check denied login
    if(!check_credentials(user, pass))
    {
        DEBUG_PRINT(("Credentials for %s:%s not found\n", user, pass));
        bytes_sent = send(sd, MSG_530, strlen(MSG_530), 0);
        if(bytes_sent == -1)
        {
            perror("send error at confirm login: ");
        }
        return false;
    }

    // confirm login
    if(!send_ans(sd, MSG_230, user)) 
    {
        return false;
    }

    return true;
}

/**
 *  function: execute all commands (RETR|QUIT|STOR)
 *  sd: socket descriptor
 **/

void operate(int sd, int datasd) {
    char op[CMDSIZE], param[PARSIZE];
    DEBUG_PRINT(("Inicio el operate\n"));

    while (true) {
        op[0] = param[0] = '\0';
        // check for commands send by the client if not inform and exit
        recv_cmd(sd, op, param);
        if (strcmp(op, "RETR") == 0) {
            retr(sd,datasd, param);
        } else if(strcmp(op, "STOR") == 0) {
            stor(sd, datasd, param);
        } else if (strcmp(op, "QUIT") == 0) {
            // send goodbye and close connection
            send_ans(sd, MSG_221);
            close(sd);
            close(datasd);
            break;
        } else {
            // invalid command
            // future use
        }
    }
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

void getClientIpPort(char *str, char *client_ip, int *client_port) {
    char *n1, *n2, *n3, *n4, *n5, *n6;
    int x5, x6;
    
    n1 = strtok(str, ",");
    n2 = strtok(NULL, ",");
    n3 = strtok(NULL, ",");
    n4 = strtok(NULL, ",");
    n5 = strtok(NULL, ",");
    n6 = strtok(NULL, ",");

    sprintf(client_ip, "%s.%s.%s.%s", n1, n2, n3, n4);

    x5 = atoi(n5);
    x6 = atoi(n6);
    *client_port = (256*x5)+x6;
}

int setupDataConnection(int *dsd, char *client_ip, int client_port, int server_port) {
    struct sockaddr_in cliaddr, tempaddr;

    if ((*dsd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        return -1;
    }

    // bind port for data connection to be server port + 1 by using a temporary struct sockaddr_in
    server_port++;
    tempaddr.sin_family = AF_INET;
    tempaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    tempaddr.sin_port = htons(server_port); 
    memset(&(tempaddr.sin_zero), '\0', 8); 
     
    while((bind(*dsd, (struct sockaddr*) &tempaddr, sizeof(tempaddr))) < 0) {
        server_port++;        
        tempaddr.sin_port = htons(server_port);
    }

    // initiate data connection with client ip and client port             
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port = htons(client_port);
    memset(&(cliaddr.sin_zero), '\0', 8); 
    if (inet_pton(AF_INET, client_ip, &cliaddr.sin_addr) <= 0){
        perror("inet_pton error");
        return -1;
    }

    if (connect(*dsd, (struct sockaddr *) &cliaddr, sizeof(cliaddr)) < 0) {
        perror("connect error");
        return -1;
    }

    return 0;
}

/**
 * Run with
 *         ./mysrv <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    // ignore kill signal sent from client when client finishes executing
    signal(SIGPIPE,SIG_IGN);
    // do not leave zombie processes
    signal(SIGCHLD, SIG_IGN);

    int sockfd, connfd, len; 
    struct sockaddr_in servaddr, cli; 
    char *server_portn = argv[1];

    // arguments checking
    if(argc == 2 && isValidPortNumber(server_portn))
    {
        printf("arguments are valid :D\n");
    } else
    {
        DEBUG_PRINT(("args: %d\nserverPort: %s\n", argc, server_portn));
        printf("Wrong numer of arguments or argument formatting\nExpecting: ./mysrv 7280\nWhere the first arg is the server port\n");
        exit(EXIT_FAILURE);
    }

    // reserve sockets and variables space
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    servaddr.sin_port = htons(convert(server_portn));
    memset(servaddr.sin_zero, '\0', sizeof servaddr.sin_zero);
    DEBUG_PRINT(("servaddr.sinaddr.s_addr: %d\nservaddr.sin_port: %d\n", htonl(INADDR_ANY), htons(convert(server_portn))));

    // create server socket and check errors
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    DEBUG_PRINT(("sockfd: %d\n", sockfd));
    if (sockfd == -1) { 
        printf("socket creation failed...\n"); 
        exit(EXIT_FAILURE); 
    } 
        printf("Socket successfully created..\n");

    // bind master socket and check errors
    if ((bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) { 
        printf("socket bind failed...\n"); 
        exit(EXIT_FAILURE); 
    } 
    printf("Socket successfully bind\n");

    // make it listen
    if ((listen(sockfd, 10)) != 0)
    { 
        printf("Listen failed...\n"); 
        exit(EXIT_FAILURE); 
    } 
    printf("Server listening..\n"); 

    // main loop
    while (true) {
        len = sizeof(cli); 
        connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
	printf("connfd %d PID %d\n", connfd, getpid());
        if (connfd < 0)
        {
            printf("server acccept failed... ERR CODE: %s\n", strerror(errno)); 
            exit(EXIT_FAILURE);
        } else {
            printf("Connecting with:%d\n",htons(cli.sin_port));
	    send_ans(connfd, MSG_220);
            if(authenticate(connfd)){
                int pid = fork();
                printf("pid %d\n", pid);
                if (pid == 0) {
                    close(sockfd);

                    int dsd, client_port = 0;
                    char recvline[BUFSIZE+1];
                    char client_ip[50];
                    int puerto = (atoi(argv[1]));
                    
                    // receive PORT command from the client
                    recv_cmd(connfd, "PORT", recvline);

                    // save client IP and port data
                    getClientIpPort(recvline, client_ip, &client_port);
                    DEBUG_PRINT(("new client IP %s and PORT %d\n", client_ip, client_port));
                    // open data connection
                    if((setupDataConnection(&dsd, client_ip, client_port, puerto)) < 0) {
                       printf("set up data connection ERROR\n");
                       break;
                    }
                    
                    operate(connfd, dsd);
                    close(dsd);
                    close(connfd);
                    exit(EXIT_SUCCESS);
                }
            }

    }

    close(connfd);
}
    return 0;
}
