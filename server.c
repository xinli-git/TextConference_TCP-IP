/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.c
 * Author: lixin39
 *
 * Created on November 6, 2017, 3:31 PM
 */
#define debug
#include <stdio.h>
#include <stdlib.h>


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include <netdb.h>

/*
 * 
 */
#define MAX_NAME 100
#define MAX_DATA 1024
#define MAX_SESSION_NUM 100
#define MAIN_PORT "8080"
#define MAX_CLIENT_SIZE 1000

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

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

struct Users {
    char userNames[MAX_CLIENT_SIZE][MAX_NAME];
    char passwords[MAX_CLIENT_SIZE][MAX_NAME];
    char curUsers[MAX_CLIENT_SIZE][MAX_NAME];
    int curUserFDs[MAX_CLIENT_SIZE];
    int nextUser;
    int num_registered_users;

};

//users in an active session, contains user name and the socket it connects to 

struct UserInSession {
    char username[MAX_NAME];
    int user_fd;
    struct UserInSession* next;
};

//a linked list of all users connected to this session, and the name of this session

struct Session {
    char sessionId[MAX_NAME];
    struct UserInSession* users_head;

};

//manages the session , stores all sessions in a static array 
//limited to have 100 sessions max, memory waste but easier

struct SessionManager {
    struct Session activeSessions[MAX_SESSION_NUM];
    int nextSessionNum;

};

//upon new coming connections, handle the connection request and if 
//request is valid, establish a persistent connection for that new user
bool establishConnection(int, fd_set*, int*);

//upon receiving new messages from existing connections, handle their request
//in this program, each user is likely from a unique host
bool handleExistingClient(int, fd_set*, int*, int);

//registers the user in the user list, check if username and password are valid
int registerUser(char*, char*, int);
//remove user from logged in users
int removeUser(char*);

//creates a new session with the user who created it
int createSession(int);

//adds additional users to the session
int add_user_to_session(int);

//remove user from session, if user is the last person, remove session as well
int remove_user_in_session(int);

int remove_user_in_all_session(int);

void broadcast(fd_set* master, int fdmax, int main_listener, char* message, char* source);

struct Message clientRequest, serverResponse;

struct Users users;
struct SessionManager sessionMaster;

int main(int argc, char** argv) {

    int main_listener;
    int next_fd;
    int yes = 1;
    int fdmax, i;
    struct addrinfo hints, *serverinfo, *p;
    int rv;

    //user initialization
    users.nextUser = 0;
    char* registered_users[] = {"nix", "anqi", "alice", "bob", "shirley"};
    char* registered_pswds[] = {"1230", "1231", "1232", "1233", "shuaigedoushiwode"};
    users.num_registered_users = 5;
    for (i = 0; i < users.num_registered_users; ++i) {

        strcpy((users.userNames)[i], registered_users[i]);
        strcpy((users.passwords)[i], registered_pswds[i]);
    }

    //session initialization, no sessions
    sessionMaster.nextSessionNum = 0;
    sessionMaster.activeSessions[0].users_head = NULL;


    //find the socket address to bind to
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP


#ifdef debug
    argv[1] = "8080";
#endif 
    //get my address information to create a main socket to listen to
    if ((rv = getaddrinfo(NULL, argv[1], &hints, &serverinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = serverinfo; p != NULL; p = p->ai_next) {

        if ((main_listener = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        //this is really optional but we have it here to see how it goes(allows multiple socket to bind to same port)
        if (setsockopt(main_listener, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof (int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(main_listener, p->ai_addr, p->ai_addrlen) == -1) {
            close(main_listener);
            perror("server: bind");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "select server: failed to bind\n");
        exit(2);
    }
    char hostaddr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(((struct sockaddr_in*) p)->sin_addr), hostaddr, INET_ADDRSTRLEN);

    printf("Listener successfully running...\n");

    freeaddrinfo(serverinfo); // all done with this

    if (listen(main_listener, MAX_CLIENT_SIZE) == -1) {
        perror("listen");
        exit(3);
    }

    //these are the file descriptors we will be monitoring 
    fd_set master; // master file descriptor list
    fd_set read_fds; // temp file descriptor list for select()

    //add our main listener to the master set 
    FD_SET(main_listener, &master);
    fdmax = main_listener; // so far, it's this one


    //main execution
    while (1) {
        read_fds = master;
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        for (i = 0; i < fdmax + 1; ++i) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == main_listener) {
                    //handle new possible connections
                    if (establishConnection(main_listener, &master, &fdmax))
                        printf("connection established\n\n");
                    else {
                        printf("connection not established\n\n");

                    }

                } else { //existing connection

                    if (handleExistingClient(i, &master, &fdmax, main_listener)) {
                        printf("user request successfully handled\n\n");

                    } else {
                        printf("user request not properly handled\n\n");
                    }
                }
            }
        }

    }

    return (EXIT_SUCCESS);
}

bool establishConnection(int main_listener, fd_set *master, int* fdmax) {

    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;
    char remoteIP[INET6_ADDRSTRLEN];
    addrlen = sizeof remoteaddr;

    int newfd = accept(main_listener,
            (struct sockaddr *) &remoteaddr,
            &addrlen);

    if (newfd == -1) {
        perror("accept");
        close(newfd);
        return false;
    } else {


        const char* incoming_addr;
        incoming_addr = inet_ntop(remoteaddr.ss_family,
                get_in_addr((struct sockaddr*) &remoteaddr),
                remoteIP, INET6_ADDRSTRLEN);

        printf("select server: new connection from %s on "
                "socket %d\n",
                incoming_addr,
                newfd);

        int numbytes;
        if ((numbytes = recv(newfd, &clientRequest, sizeof (struct Message), 0)) == -1) {
            perror("recv");
            close(newfd);
            return false;
        }

        if (clientRequest.cmd == LOGIN) {
            int registerCode;
            registerCode = registerUser(clientRequest.source, clientRequest.data, newfd);

            //user is new, need to check if the user name is already logged in
            if (registerCode == 0) {
                printf("new user: %s\n", clientRequest.source);

                serverResponse.cmd = LO_ACK;
                strcpy(serverResponse.data, "login successful for user: ");
                strcat(serverResponse.data, clientRequest.source);

                strcpy(serverResponse.source, clientRequest.source);
                serverResponse.size = sizeof (serverResponse.data);
                //send the success message back
                if (send(newfd, &serverResponse, sizeof (struct Message), 0) == -1)
                    perror("send");

                //this user is on the white list, I will listen to it and keep it as
                //max fd if it happens to be so   
                if (newfd > *fdmax) { // keep track of the max
                    *fdmax = newfd;
                }
                FD_SET(newfd, master);
                char broadMes[MAX_DATA];
                strcpy(broadMes, "User: ");
                strcat(broadMes, clientRequest.source);
                strcat(broadMes, ", just logged in\n");
                broadcast(master, *fdmax, main_listener, broadMes, "Server");

                return true;
            }//the user already existed
            else if (registerCode == -1) {
                //send nack
                serverResponse.cmd = LO_NACk;
                strcpy(serverResponse.data, "user already logged in\n");
                strcpy(serverResponse.source, clientRequest.source);
                serverResponse.size = sizeof (serverResponse.data);

                //user already logged in
                if (send(newfd, &serverResponse, sizeof (struct Message), 0) == -1)
                    perror("send");
                close(newfd);
                return false;
            }//send nack, wrong password
            else if (registerCode == -2) {
                serverResponse.cmd = LO_NACk;
                strcpy(serverResponse.data, "Incorrect password\n");
                strcpy(serverResponse.source, clientRequest.source);
                serverResponse.size = sizeof (serverResponse.data);

                if (send(newfd, &serverResponse, sizeof (struct Message), 0) == -1)
                    perror("send");
                close(newfd);
                return false;

            } else if (registerCode == -3) {
                serverResponse.cmd = LO_NACk;
                strcpy(serverResponse.data, "Username does not exist\n");
                strcpy(serverResponse.source, clientRequest.source);
                serverResponse.size = sizeof (serverResponse.data);

                if (send(newfd, &serverResponse, sizeof (struct Message), 0) == -1)
                    perror("send");
                close(newfd);
                return false;
            } else {

                serverResponse.cmd = LO_NACk;
                strcpy(serverResponse.data, "Unknown Error\n");
                strcpy(serverResponse.source, clientRequest.source);
                serverResponse.size = sizeof (serverResponse.data);

                if (send(newfd, &serverResponse, sizeof (struct Message), 0) == -1)
                    perror("send");
                close(newfd);
                return false;
            }

        } else {
            serverResponse.cmd = LO_NACk;
            strcpy(serverResponse.data, "New user must login first before any new command\n");
            strcpy(serverResponse.source, clientRequest.source);
            serverResponse.size = sizeof (serverResponse.data);

            if (send(newfd, &serverResponse, sizeof (struct Message), 0) == -1)
                perror("send");
            close(newfd);
            return false;

        }

    }
}

bool handleExistingClient(int clientfd, fd_set *master, int* fdmax, int main_listener) {

    int nbytes, j;

    //error handling 
    if ((nbytes = recv(clientfd, &clientRequest, sizeof (struct Message), 0)) <= 0) {
        // got error or connection closed by client
        if (nbytes == 0) {
            // connection closed
            printf("select_server: socket %d hung up\n", clientfd);
        } else {
            perror("recv");
        }

        for (int i = 0; i < users.nextUser; ++i) {
            if (users.curUserFDs[i] == clientfd)
                remove_user_in_session(clientfd);
            removeUser(users.curUsers[i]);

        }

        close(clientfd); // bye!
        FD_CLR(clientfd, master); // remove from master set
        printf("connection at socket: %d erased due to network error\n", clientfd);
        return false;
    }//actual data received
    else {
        //handle various user request
        switch (clientRequest.cmd) {
            case LOGOUT:
            {
                //user has active session, need to quit that

                if (remove_user_in_all_session(clientfd) == 0) {
                    printf("also removed user from all of his/her sessions\n");
                }
                //user do not have active session, just remove him
                removeUser(clientRequest.source);
                serverResponse.cmd = LT_ACK;
                serverResponse.size = 0;
                strcpy(serverResponse.source, clientRequest.source);
                if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                    perror("send");
                close(clientfd); // bye!
                FD_CLR(clientfd, master); // remove from master set

                printf("logged out: %s\n", serverResponse.source);

                char broadMes[MAX_DATA];
                strcpy(broadMes, "User: ");
                strcat(broadMes, clientRequest.source);
                strcat(broadMes, ", just logged out\n");
                broadcast(master, *fdmax, main_listener, broadMes, "Server");

                break;
            }
                //client wants to create a new session
            case NEW_SESS:
            {
                int session_creation = createSession(clientfd);

                if (session_creation == 0) {
                    printf("user: %s created session: %s\n", clientRequest.source, clientRequest.data);
                    //send back success message
                    serverResponse.cmd = NS_ACk;
                    strcpy(serverResponse.data, "session created, id, user: ");
                    strcat(serverResponse.data, clientRequest.data);
                    strcat(serverResponse.data, ", ");
                    strcat(serverResponse.data, clientRequest.source);
                    strcpy(serverResponse.source, clientRequest.source);
                    serverResponse.size = strlen(serverResponse.data) + 1;
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");
                    char tempmes[MAX_DATA];
                    strcpy(tempmes, "User: ");
                    strcat(tempmes, clientRequest.source);
                    strcat(tempmes, " just created session: ");
                    strcat(tempmes, clientRequest.data);

                    broadcast(master, *fdmax, main_listener, tempmes, "Server");


                } else if (session_creation == -1) {
                    printf("session creation failed, session already exists\n");
                    //session already existed
                    serverResponse.cmd = NS_NACK;
                    strcpy(serverResponse.data, "session already exists");
                    strcpy(serverResponse.source, clientRequest.source);
                    serverResponse.size = strlen(serverResponse.data) + 1;
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");

                } else {
                    printf("session creation failed, too many concurrent sessions \n");
                    //session number max out
                    serverResponse.cmd = NS_NACK;
                    strcpy(serverResponse.data, "Server concurrent session capacity maxed out");
                    strcpy(serverResponse.source, clientRequest.source);
                    serverResponse.size = strlen(serverResponse.data) + 1;
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");
                }

                break;
            }
            case LEAVE_SESS:
            {

                //leave the current session, check if the user is in the session
                //maybe not used in this lab but later on need to support multiple 
                //sessions.

                //also check if this user is the last user in the session, if yes, delete the
                //session and notify all others as well
                int leave = remove_user_in_session(clientfd);
                if (leave == 0) {

                    printf("user %s left session %s\n", clientRequest.source, clientRequest.data);

                    serverResponse.cmd = LV_ACK;
                    strcpy(serverResponse.data, clientRequest.data);
                    strcpy(serverResponse.source, clientRequest.source);
                    serverResponse.size = strlen(serverResponse.data) + 1;
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");

                } else if (leave == -1) {
                    printf("user %s not in session %s\n", clientRequest.source, clientRequest.data);

                    serverResponse.cmd = LVNACK;
                    strcpy(serverResponse.data, "User not in session\n");
                    strcpy(serverResponse.source, clientRequest.source);
                    serverResponse.size = strlen(serverResponse.data) + 1;
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");

                } else {
                    printf("session not found\n");

                    serverResponse.cmd = LVNACK;
                    strcpy(serverResponse.data, "session not found\n");
                    strcpy(serverResponse.source, clientRequest.source);
                    serverResponse.size = strlen(serverResponse.data) + 1;
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");

                }


                break;
            }
            case JOIN:
            {
                //check if the session exists, then check if the user is in session 
                int join = add_user_to_session(clientfd);
                //join successful
                if (join == 0) {

                    printf("User: %s joined session %s\n", clientRequest.source, clientRequest.data);
                    serverResponse.cmd = JN_ACK;
                    strcpy(serverResponse.source, clientRequest.source);
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");

                } else if (join == -1) {
                    printf("Join session failed, session does not exist\n");
                    serverResponse.cmd = JN_NACK;
                    strcpy(serverResponse.data, "Join session failed, session does not exist\n");
                    strcpy(serverResponse.source, clientRequest.source);
                    serverResponse.size = strlen(serverResponse.data) + 1;
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");
                } else {
                    printf("Join session failed, user already in session\n");
                    serverResponse.cmd = JN_NACK;
                    strcpy(serverResponse.data, "Join session failed, user already in session\n");
                    strcpy(serverResponse.source, clientRequest.source);
                    serverResponse.size = strlen(serverResponse.data) + 1;
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");

                }
                break;
            }
            case QUERY:
            {
                serverResponse.cmd = QU_ACK;
                strcpy(serverResponse.data,"Users: ");
                for(int i =0;i<users.nextUser;++i){
                    strcat(serverResponse.data, users.curUsers[i]);
                    strcat(serverResponse.data, " ");
                    
                }
                strcat(serverResponse.data, "\n\nSessions: \n");
                for (int i = 0; i < sessionMaster.nextSessionNum; ++i) {
                    strcat(serverResponse.data, "\nID: ");
                    strcat(serverResponse.data, sessionMaster.activeSessions[i].sessionId);
                    strcat(serverResponse.data, "\n");
                    struct UserInSession* p;
                    for (p = sessionMaster.activeSessions[i].users_head;
                            p != NULL; p = p->next) {
                        char userinfo[MAX_NAME + 10];
                        sprintf(userinfo, "user: %s\n", p->username);
                        strcat(serverResponse.data, userinfo);
                    }
                }
                if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                    perror("send");

                break;

            }
            case EXIT:
            {
                if (remove_user_in_all_session(clientfd) == 0)
                    printf("also removed user from all the sessions\n");
                if (removeUser(clientRequest.source) <=1) {
                    printf("user logged out\n");
                    serverResponse.cmd = QU_ACK;
                    strcpy(serverResponse.source, clientRequest.source);
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");
                } else {
                    serverResponse.cmd = LT_NACK;
                    strcpy(serverResponse.source, clientRequest.source);
                    strcpy(serverResponse.data, "logout unsuccessful");
                    if (send(clientfd, &serverResponse, sizeof (struct Message), 0) == -1)
                        perror("send");
                }
                break;
            }
            case MESSAGE:
            {
                if (strcmp(clientRequest.source,"all")==0){
                    
                    for (int i = 0; i < sessionMaster.nextSessionNum; ++i) {

                        struct UserInSession *p = sessionMaster.activeSessions[i].users_head;
                        for (; p != NULL; p = p->next) {
                            if (send(p->user_fd, &clientRequest, sizeof (struct Message), 0) == -1)
                                perror("send");
                        }
                    }
                    
                }            
                else
                {
                    for (int i = 0; i < sessionMaster.nextSessionNum; ++i) {
                        if (strcmp(sessionMaster.activeSessions[i].sessionId, clientRequest.source) == 0) {

                            struct UserInSession *p = sessionMaster.activeSessions[i].users_head;
                            for (; p != NULL; p = p->next) {
                                if (send(p->user_fd, &clientRequest, sizeof (struct Message), 0) == -1)
                                    perror("send");
                            }
                        }
                    }
                }
                break;
            }



            default:
            {
                printf("functionality not yet implemented\n");
                break;
            }
        }

    }

}

int registerUser(char* username, char* password, int fd) {
    //handle max user logins when you have time, but do not think this will
    //ever be a problem in this lab
    for (int i = 0; i < users.nextUser; ++i) {
        if (strcmp(username, (users.curUsers)[i]) == 0) {
            //user already logged in
            return -1;
        }
    }

    for (int i = 0; i < users.num_registered_users; ++i) {
        if (strcmp(username, (users.userNames)[i]) == 0) {
            if (strcmp(password, (users.passwords)[i]) == 0) {
                //user is valid 
                strcpy(users.curUsers[users.nextUser], username);
                users.curUserFDs[users.nextUser] = fd;
                users.nextUser++;
                //successful register
                return 0;

            } else {
                //wrong password
                return -2;
            }

        }
    }
    //username not registered
    return -3;

}

int removeUser(char* username) {
    //again, this is very sloppy, but get it done quick
    for (int i = 0; i < users.nextUser; ++i) {
        if (strcmp(username, (users.curUsers)[i]) == 0) {

            for (int j = i; j < users.nextUser; ++j) {
                strcpy(users.curUsers[j], users.curUsers[j + 1]);
                users.curUserFDs[j] = users.curUserFDs[j + 1];
            }
            users.nextUser--;
            return 0;
        }

    }
    //user not found, but we do not care anyways
    return 1;
}

int createSession(int curfd) {

    //redundancy check;
    if (clientRequest.cmd != NEW_SESS) {
        return -3;
    }
    if (sessionMaster.nextSessionNum >= MAX_SESSION_NUM) {
        //session number full
        return -2;
    }
    for (int i = 0; i < sessionMaster.nextSessionNum; ++i) {

        if (strcmp(sessionMaster.activeSessions[i].sessionId, clientRequest.data) == 0) {
            //session already created
            return -1;
        }
    }
    struct Session news;
    strcpy(news.sessionId, clientRequest.data);
    news.users_head = malloc(sizeof (struct UserInSession));
    news.users_head->user_fd = curfd;
    strcpy(news.users_head->username, clientRequest.source);
    news.users_head->next = NULL;

    sessionMaster.activeSessions[sessionMaster.nextSessionNum] = news;
    sessionMaster.nextSessionNum++;
    return 0;
}

void broadcast(fd_set* master, int fdmax, int main_listener, char* message, char* source) {
    //for broad casting the other users
    int j;

    // we got some data from a client
    for (j = 0; j <= fdmax; j++) {
        // send to everyone!
        if (FD_ISSET(j, master)) {
            // except the listener and ourselves
            if (j != main_listener) {
                struct Message sendms;
                sendms.cmd = MESSAGE;
                strcpy(sendms.data, message);
                strcpy(sendms.source, source);
                sendms.size = strlen(message) + 1;

                if (send(j, &sendms, sizeof (struct Message), 0) == -1) {
                    perror("send");
                }
            }
        }
    }

}

int add_user_to_session(int cur_fd) {
    //sanity check 
    if (clientRequest.cmd != JOIN) {
        return -5;
    }

    for (int i = 0; i < sessionMaster.nextSessionNum; ++i) {
        //session id is valid
        if (strcmp(sessionMaster.activeSessions[i].sessionId, clientRequest.data) == 0) {

            struct UserInSession* p = sessionMaster.activeSessions[i].users_head;

            while (p->next != NULL) {
                if (strcmp(p->username, clientRequest.source) == 0)
                    //user already in session 
                    return -2;
                p = p->next;
            }
            if (strcmp(p->username, clientRequest.source) == 0) {
                return -2;
            }
            p->next = malloc(sizeof (struct UserInSession));
            p = p->next;
            p->user_fd = cur_fd;
            strcpy(p->username, clientRequest.source);
            p->next = NULL;

            //user successfully joined 
            return 0;
        }
    }

    //no such session id
    return -1;
}

int remove_user_in_session(int curfd) {

    //leave the current session, check if the user is in the session
    //maybe not used in this lab but later on need to support multiple 
    //sessions.

    //also check if this user is the last user in the session, if yes, delete the
    //session and notify all others as well

    for (int i = 0; i < sessionMaster.nextSessionNum; ++i) {

        //session exists
        if (strcmp(sessionMaster.activeSessions[i].sessionId, clientRequest.data) == 0) {

            //in the session user wants to remove from
            struct Session* session_to_remove = &sessionMaster.activeSessions[i];
            //get the linked list of all users 
            struct UserInSession *p = session_to_remove->users_head;
            struct UserInSession *previous = p;

            while (p != NULL) {
                //if this is the user we want to remove
                if (strcmp((p)->username, clientRequest.source) == 0) {
                    //remove head
                    if (p == session_to_remove->users_head) {
                        sessionMaster.activeSessions[i].users_head = p->next;
                        free(p);

                    } else if (p->next == NULL) {
                        free(p);
                        previous->next = NULL;
                    } else {
                        previous->next = p->next;
                        free(p);
                        p = NULL;
                    }
                    if (sessionMaster.activeSessions[i].users_head == NULL) {

                        //remove session
                        for (int j = i; j < sessionMaster.nextSessionNum; ++j) {
                            sessionMaster.activeSessions[j] = sessionMaster.activeSessions[j + 1];

                        }
                        sessionMaster.nextSessionNum--;
                        return 0;
                    } else {
                        return 0;
                    }

                }//end delete user
                else {
                    previous = p;
                    p = p->next;
                }
            }//end traverse user

            //user not in session
            return -1;
        }

    }
    //session requested does not exist
    return -2;

}

int remove_user_in_all_session(int curfd) {

    //leave the current session, check if the user is in the session
    //maybe not used in this lab but later on need to support multiple 
    //sessions.

    //also check if this user is the last user in the session, if yes, delete the
    //session and notify all others as well



    for (int i = 0; i < sessionMaster.nextSessionNum; ++i) {


        //in the session user wants to remove from
        struct Session* session_to_remove = &sessionMaster.activeSessions[i];
        //get the linked list of all users 
        struct UserInSession *p = session_to_remove->users_head;
        struct UserInSession *previous = p;

        while (p != NULL) {
            //if this is the user we want to remove
            if (strcmp((p)->username, clientRequest.source) == 0) {
                //remove head
                if (p == session_to_remove->users_head) {
                    sessionMaster.activeSessions[i].users_head = p->next;
                    free(p);

                } else if (p->next == NULL) {
                    free(p);
                    previous->next = NULL;
                } else {
                    previous->next = p->next;
                    free(p);
                    p = NULL;
                }
                if (sessionMaster.activeSessions[i].users_head == NULL) {

                    //remove session
                    for (int j = i; j < sessionMaster.nextSessionNum; ++j) {
                        sessionMaster.activeSessions[j] = sessionMaster.activeSessions[j + 1];

                    }
                    sessionMaster.nextSessionNum--;
                    i--;
                    break;
                }

            }//end delete user
            else {
                previous = p;
                p = p->next;
            }
        }//end traverse user

    }
    //session requested does not exist
    return 0;

}


