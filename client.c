

/* 
 * File:   main.c
 * Author: lixin
 *
 * Created on November 6, 2017, 3:31 PM
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <stdbool.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#define MAX_NAME 100
#define MAX_DATA 1024
#define MAX_SESSIONS 128
//////////////////////////////User state definitions////////////////////////////
#define LOGGED_IN 0
#define LOGGED_OUT 1
#define IN_SESSION 2

enum Command {
    LOGIN, LO_ACK, LO_NACk,
    LOGOUT, LT_ACK, LT_NACK,
    EXIT,
    JOIN, JN_ACK, JN_NACK,
    LEAVE_SESS, NEW_SESS, NS_ACk, NS_NACK,
    LV_ACK, LVNACK,
    MESSAGE,
    QUERY, QU_ACK
};

struct Message {
    enum Command cmd;
    unsigned int size;
    unsigned char source[MAX_NAME];
    unsigned char data[MAX_DATA];
};

struct User {
    char username[MAX_NAME];
    char password[MAX_NAME];
    int state;
    int next_session;
    char cursession[MAX_SESSIONS][MAX_NAME];
} curuser;

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*) sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

bool parse_login(char* input, char** addr, char** port, char** username, char** password);
void logoutUser(int fd);
void exit_handler();

struct Message userRequest, serverResponse;
int sock_global;

int main(void) {
    atexit(exit_handler);
    printf("Welcome!\nFollow the prompts to join the exclusive text conference :)\n");
    int sockfd, numbytes;
    char buf[MAX_DATA];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;


    //do parser here, fill the information of connecting to the server 
    char line[MAX_DATA - MAX_NAME];
    char sessionID[MAX_NAME];

    curuser.state = LOGGED_OUT;
    curuser.next_session = 0;

    //to do stdin and fd select
    while (1) {

        /****************************** Parse Successful Login**************************/
        printf("log in first, type: /login <username> <password> <serverip> <port number>\n");

        fgets(line, sizeof (line), stdin); /* read in a line */
        bool login = false;
        char log_in[10] = "/login";
        char first[10];

        //char* a,p,u,pswd;
        char tempA[50], tempP[50], tempU[50], tempPswd[50];
        memset(&tempA, 0, 50);
        memset(&tempP, 0, 50);
        memset(&tempU, 0, 50);
        memset(&tempPswd, 0, 50);
        sscanf(line, "%s", first);
        if (strcmp(first, log_in) == 0) {

            sscanf(line, "%s %s %s %s %s", first, tempU, tempPswd, tempA, tempP);

            if (strcmp(tempU, "") == 0 || strcmp(tempPswd, "") == 0 || strcmp(tempA, "") == 0 || strcmp(tempP, "") == 0) {
                login = false;
            } else {
                login = true;
            }
        } else {
            login = false;
        }
        //handle user command if user successfully logged in

        /****************************** End of Parse Successful Login**************************/


        //login parse right
        if (login) {
            /****************************** Log in server request***********************************/


            if ((rv = getaddrinfo(tempA, tempP, &hints, &servinfo)) != 0) {
                fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
                continue;
            }


            // loop through all the results and connect to the first we can
            for (p = servinfo; p != NULL; p = p->ai_next) {
                if ((sockfd = socket(p->ai_family, p->ai_socktype,
                        p->ai_protocol)) == -1) {
                    perror("client: socket");
                    continue;

                }
                if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                    close(sockfd);
                    perror("client: connect");
                    continue;
                }
                break;
            }


            if (p == NULL) {
                fprintf(stderr, "client: failed to connect\n");
                continue;
            }

            inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr),
                    s, sizeof s);
            printf("client: connecting to %s\n", s);
            freeaddrinfo(servinfo); // all done with this structure


            userRequest.cmd = LOGIN;

            strcpy(userRequest.data, tempPswd);
            userRequest.size = sizeof (userRequest.data);
            strcpy(userRequest.source, tempU);


            if (send(sockfd, &userRequest, sizeof (struct Message), 0) == -1) {
                perror("send");
                continue;
            }


            if ((numbytes = recv(sockfd, &serverResponse, sizeof (struct Message), 0)) == -1) {
                perror("recv");
                continue;
            }

            if (serverResponse.cmd != LO_ACK) {
                printf(serverResponse.data);
                printf("\nplease log in again\n");
                continue;

            } else if (serverResponse.cmd == LO_ACK) {
                sock_global = sockfd;
                printf(serverResponse.data);
                printf("\n");
                strcpy(curuser.password, tempPswd);
                strcpy(curuser.username, tempU);
                curuser.state = LOGGED_IN;
                curuser.next_session = 0;
                memset(curuser.cursession[0], 0, MAX_NAME);
                printf("\n************************************ Entering Text Conference Room **************************\n\n");
                printf("hello %s, you are now logged in\n", curuser.username);
                fd_set listen, temp;
                FD_ZERO(&listen);
                FD_SET(STDIN_FILENO, &listen);
                FD_SET(sockfd, &listen);
                printf("Please input subsequent command at the command line\n");

                while (1) {
                    //user interaction after login
                    temp = listen;
                    struct timeval timeout;
                    timeout.tv_usec = (100000);
                    printf("************************* Current Sessions: ");
                    if (curuser.next_session == 0) {
                        printf("None");
                    } else {
                        for (int i = 0; i < curuser.next_session; ++i) {
                            if (i < curuser.next_session - 1) {
                                printf("%d: %s, ", i + 1, curuser.cursession[i]);
                            } else {
                                printf("%d: %s ", i + 1, curuser.cursession[i]);
                            }
                        }
                    }
                    printf(" **************************\n\n");

                    if (select(sockfd + 1, &temp, NULL, NULL, NULL) <= 0)continue;


                    if (FD_ISSET(sockfd, &temp)) {
                        //do broadcasting
                        if ((recv(sockfd, &serverResponse, sizeof (struct Message), 0)) == -1) {
                            perror("recv");
                            continue;
                        }
                        //FD_CLR(sockfd, &listen);
                        if (serverResponse.cmd != MESSAGE)continue;
                        else {
                            printf("\n****Message from %s: \n", serverResponse.source);
                            printf("****%s", serverResponse.data);
                            printf("\n");
                        }
                    }

                    if (FD_ISSET(STDIN_FILENO, &temp)) {
                        memset(line, 0, MAX_DATA - MAX_NAME);
                        read(STDIN_FILENO, line, 900);
                        //FD_CLR(STDIN_FILENO, &listen);

                        char first[50];
                        memset(&first, 0, 50);
                        sscanf(line, "%s", first);

                        if (strcmp(first, "/logout") == 0) {

                            logoutUser(sockfd);
                            //exit the server
                            curuser.state = LOGGED_OUT;
                            memset(curuser.username, 0, MAX_NAME);
                            memset(curuser.password, 0, MAX_NAME);
                            for (int i = 0; i < curuser.next_session; ++i) {
                                //clean up all the sessions
                                memset(curuser.cursession[i], 0, MAX_NAME);

                            }
                            curuser.next_session = 0;
                            printf("\n************************************ Existing Text Conference Room **************************\n\n");

                            break;



                        } else if (strcmp(first, "/joinsession") == 0) {

                            //join the conference session with the given session id
                            sscanf(line, "%s %s", first, sessionID);
                            userRequest.cmd = JOIN;
                            strcpy(userRequest.data, sessionID);
                            strcpy(userRequest.source, curuser.username);
                            userRequest.size = strlen(sessionID) + 1;
                            if (send(sockfd, &userRequest, sizeof (struct Message), 0) == -1) {
                                perror("send");
                                continue;
                            }

                            if (recv(sockfd, &serverResponse, sizeof (struct Message), 0) == -1) {
                                perror("recv");
                                continue;
                            }

                            if (serverResponse.cmd == JN_ACK) {
                                printf("joining session %s\n", userRequest.data);

                                curuser.state = IN_SESSION;
                                strcpy(curuser.cursession[curuser.next_session], sessionID);
                                curuser.next_session++;
                            } else if (serverResponse.cmd == JN_NACK) {
                                printf("Joining session unsuccessful\n");
                                printf("Reason: %s", serverResponse.data);

                            } else {
                                printf("Join session: unknown response, please input again\n");
                            }

                        } else if (strcmp(first, "/leavesession") == 0) {

                            sscanf(line, "%s %s", first, sessionID);

                            if (curuser.state != IN_SESSION) {
                                printf("User not in session, please join a session first\n");

                            } else {
                                //leave the session on the remove server

                                userRequest.cmd = LEAVE_SESS;
                                strcpy(userRequest.data, sessionID);
                                strcpy(userRequest.source, curuser.username);
                                userRequest.size = strlen(userRequest.data) + 1;

                                if (send(sockfd, &userRequest, sizeof (struct Message), 0) == -1) {
                                    perror("send");
                                    continue;
                                }

                                if (recv(sockfd, &serverResponse, sizeof (struct Message), 0) == -1) {
                                    perror("recv");
                                    continue;
                                }
                                if (serverResponse.cmd == LV_ACK) {
                                    printf("User %s successfully left session: %s\n", serverResponse.source, serverResponse.data);

                                    for (int i = 0; i < curuser.next_session; ++i) {
                                        if (strcmp(curuser.cursession[i], sessionID) == 0) {
                                            memset(curuser.cursession[i], 0, MAX_NAME);
                                            for (int j = i; j < curuser.next_session; j++) {

                                                strcpy(curuser.cursession[j], curuser.cursession[j + 1]);

                                            }

                                            break;
                                        }
                                    }
                                    curuser.next_session--;
                                    if (curuser.next_session == 0) {
                                        curuser.state = LOGGED_IN;
                                    }

                                } else if (serverResponse.cmd == LVNACK) {
                                    printf("Leaving session unsuccessful\n");
                                    printf("Reason: %s, \n", serverResponse.data);

                                } else {
                                    printf("Undefined server behavior, please input again\n");
                                }



                            }

                        } else if (strcmp(first, "/createsession") == 0) {

                            //create a new conference session and join it
                            sscanf(line, "%s %s", first, sessionID);

                            userRequest.cmd = NEW_SESS;
                            strcpy(userRequest.data, sessionID);
                            strcpy(userRequest.source, curuser.username);
                            userRequest.size = strlen(sessionID) + 1;
                            if (send(sockfd, &userRequest, sizeof (struct Message), 0) == -1) {
                                perror("send");
                                continue;
                            }

                            if (recv(sockfd, &serverResponse, sizeof (struct Message), 0) == -1) {
                                perror("recv");
                                continue;
                            }
                            if (serverResponse.cmd == NS_ACk) {
                                printf("joining session %s\n", userRequest.data);
                                curuser.state = IN_SESSION;
                                strcpy(curuser.cursession[curuser.next_session], sessionID);
                                curuser.next_session++;

                            } else {
                                printf("Session id: %s not created\n", userRequest.data);
                                printf("Reason: %s\n", serverResponse.data);

                            }


                        } else if (strcmp(first, "/list") == 0) {
                            userRequest.cmd = QUERY;
                            strcpy(userRequest.source, curuser.username);

                            if (send(sockfd, &userRequest, sizeof (struct Message), 0) == -1) {
                                perror("send");
                                continue;
                            }

                            if (recv(sockfd, &serverResponse, sizeof (struct Message), 0) == -1) {
                                perror("recv");
                                continue;
                            }
                            if (serverResponse.cmd == QU_ACK) {
                                printf("\n\n*********************** printing text conference information ****************\n\n");
                                printf(serverResponse.data);
                                printf("\n\n**************************** END OF MESSAGES ****************************\n\n");

                            } else {
                                printf("Undefined behavior from server, please input again\n");

                            }

                        } else if (strcmp(first, "/quit") == 0) {

                            userRequest.cmd = EXIT;
                            strcpy(userRequest.data, curuser.username);
                            strcpy(userRequest.source, curuser.username);

                            if (send(sockfd, &userRequest, sizeof (struct Message), 0) == -1) {
                                perror("send");
                                continue;
                            }

                            if (recv(sockfd, &serverResponse, sizeof (struct Message), 0) == -1) {
                                perror("recv");
                                continue;
                            }

                            if (serverResponse.cmd == QU_ACK) {
                                printf("User quitted safely\n");
                                exit(0);
                            } else {
                                printf("Uncaught behavior from server, please input again\n");

                            }

                        } else {



                            //get text from client
                            if (curuser.state != IN_SESSION) {
                                printf("User not joined in any session, message delivery failed\n");

                            } else {
                                printf("deliver to which session? \ninput all if you wish to send to all of your sessions");
                                printf("\n>");
                                scanf("%s", sessionID);
                                bool inThis = false;
                                for (int i = 0; i < curuser.next_session; ++i) {
                                    if (strcmp(curuser.cursession[i], sessionID) == 0) {
                                        inThis = true;
                                        break;
                                    }

                                }
                                if (inThis) {

                                    memset(&userRequest.data, 0, MAX_DATA);
                                    userRequest.cmd = MESSAGE;
                                    strcpy(userRequest.data, curuser.username);
                                    strcat(userRequest.data, " said: ");
                                    strcat(userRequest.data, line);
                                    strcpy(userRequest.source, sessionID);
                                    if (send(sockfd, &userRequest, sizeof (struct Message), 0) == -1) {
                                        perror("send");
                                        continue;
                                    }
                                } 
                                else {
                                    printf("You are not in session: %s, please join first\n", sessionID);
                                }

                            }
                        }
                    }

                }
            } else {
                printf("Unknown response\n");
                continue;
            }


        } else {
            printf("command not valid, please log in first\n");
        }
    }

    //if (strcmp(buf))
    close(sockfd);
    return 0;
}

bool parse_login(char* input, char** addr, char** port, char** username, char** password) {
    char log_in[10] = "/login";
    char first[10];

    //char* a,p,u,pswd;
    char tempA[50], tempP[50], tempU[50], tempPswd[50];

    sscanf(input, "%s", first);
    printf(input);
    if (strcmp(first, log_in) == 0) {

        sscanf(input, "%s %s %s %s %s", first, tempU, tempPswd, tempA, tempP);

        if (strcmp(tempU, "") == 0 || strcmp(tempPswd, "") == 0 || strcmp(tempA, "") == 0 || strcmp(tempP, "") == 0) {
            printf("Login unsuccess. Please follow the correct command to login. \n");
            return false;

        } else {

            return true;
        }
    } else {
        printf("Login unsuccess. \n");
        return false;
    }
}

void logoutUser(int fd) {
    if (curuser.state == LOGGED_OUT) {
        printf("Users already logged out\n");
        return;
    } else {
        int maxretry = 3;
        for (int i = 0; i < maxretry; ++i) {

            userRequest.cmd = LOGOUT;
            strcpy(userRequest.data, curuser.username);
            strcpy(userRequest.source, curuser.username);
            userRequest.size = 0;
            if (send(fd, &userRequest, sizeof (struct Message), 0) == -1) {
                perror("send");
                continue;
            }

            if ((recv(fd, &serverResponse, sizeof (struct Message), 0)) == -1) {
                perror("recv");
                continue;
            }

            if (serverResponse.cmd != LT_ACK) {
                printf("Log out error at server, trying again\n");

            } else {
                printf("user: %s successful logged out\n", serverResponse.source);
                return;
            }
        }

        printf("Fatal error, unknown logout error after 3 retries\n");
        return;

    }

}

void exit_handler() {
    userRequest.cmd = EXIT;
    strcpy(userRequest.data, curuser.username);
    strcpy(userRequest.source, curuser.username);

    send(sock_global, &userRequest, sizeof (struct Message), 0);
    printf("exiting due to interrupt or uncaught exception, sending exit info to server");


}
