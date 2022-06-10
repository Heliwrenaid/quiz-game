#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

#define PORT 9034
#define SA struct sockaddr
#define DELIMITER "##"
#define MAX_SIZE 1024

#define PLAYER_MAX_NICK_LEN 15

int sockfd;

struct questions {
   char *text;
   char **answers;
   int correct_answer;
};

void sighandler(int sig_num)
{
    signal(SIGTSTP, sighandler);
    printf("\nCannot execute Ctrl+Z\n");
}

void
sig_int_handler(int dummy) {
    close(sockfd);
}

void
parse_question(char * value) {
    int counter = -1;
    while (value != NULL) {
        if (counter == 0) {
            printf("\n\n----------\nQuestion: %s\n", value);
        } else if (counter > 0) {
            printf("  [%d] %s\n", counter, value);
        }
        
        value = strtok(NULL, DELIMITER);
        counter++;
    } 
}

void
parse_results(char * value) {
    printf("\n\n---- RESULTS ----\n");

    int counter = -1;
    while (value != NULL) {
        if (counter == 0) {
            printf("%s: ", value);
        } else if (counter == 1) {
            printf("%s\n", value);
        } else if (counter > 1) {
            counter = 0;
            continue;
        }
        
        value = strtok(NULL, DELIMITER);
        counter++;
    }
    printf("\n");
}

int
parse_message(char * message) {
    char * value = strtok(message, DELIMITER);

    if (value == NULL) {
        printf("parse error\n");
        exit(1);
    } else if(strcmp(value, "question") == 0) {
         parse_question(message);
         return 0;
    } else if(strcmp(value, "result") == 0) {
         parse_results(message);
         return 1;
    } else if(strcmp(value, "config") == 0) {
        return 2;
    } else {
        return -1;
    }

    
}

int 
send_everything(int sockfd, char * buffer, int * len) {
    int total = 0;
    int bytesleft = * len;
    int n;
   
    while(total < * len) {
        n = send(sockfd, buffer + total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }
   
    * len = total;
   
    return n ==- 1 ?- 1: 0;
}

void
send_answer(int sockfd, int asnwer) {
    char buffer[11]; 
    sprintf(buffer,"%ld", asnwer);
    int len = strlen(buffer);
    send_everything(sockfd, buffer, &len);
    
}

void
send_nick(int sockfd, char * nick) {
    char buffer[30]; 
    sprintf(buffer,"%s", nick);
    int len = strlen(nick);
    send_everything(sockfd, nick, &len);
}

void 
game_loop(int sockfd, char * player_nick)
{
    char buffer[MAX_SIZE];
    int n;
    int action_type;
    int selection;
    bool is_game_ended;

    int number_of_answers;
    int timeout;

    while(!is_game_ended) {
        
        if (n = recv(sockfd, buffer, sizeof(buffer), 0) < 0) {
            printf("Couldn't receive\n");
        }
        //printf("RECEIVED: %s\n", buffer);

        action_type = parse_message(buffer);

        switch (action_type) {
            case -1: {

                printf("Parse message error\n");
                exit(1);
            
            } break;

            case 0: {

                fd_set input_fds;
                fd_set read_fds;

                FD_SET(0, &input_fds);

                struct timeval non_block;
                non_block.tv_sec = 0;
                non_block.tv_usec = 0;

                struct timeval begin, end;
                gettimeofday(&begin, 0);
                long seconds = 0;

                char selection_c;

                for(;;) {

                    gettimeofday(&end, 0);
                    seconds = end.tv_sec - begin.tv_sec;

                    if (seconds > timeout) {
                        selection = 13;
                        break;
                    }

                    read_fds = input_fds;
                    if(select(1, &read_fds, NULL, NULL, &non_block) == -1) {
                        perror("select");
                        exit(1);
                    }
                    
                    printf("\r[%d] Select answer: ", timeout - seconds);

                    if (FD_ISSET(0, &read_fds)) {
                        scanf("%c", &selection_c);
                        selection = (int) selection_c - 48;
                        if (selection == -38) continue; // ENTER
                        if (selection < 1 || selection > number_of_answers) {
                            printf("\rWrong answer number: %d\n", selection);
                        } else {
                            printf("\r[%d] Select answer: ", timeout - seconds);
                            break;
                        }
                    }
                }
                printf("\n\nSelected answer: %d\n", selection);
                send_answer(sockfd, selection - 1);
                selection = 0;

            } break;

            case 1: is_game_ended = true;
            break;

            case 2: {
                
                int counter = -1;
                char * value = buffer;

                while (value != NULL) {
                    if (counter == 0) {
                        number_of_answers = atoi(value);
                    } else if (counter == 1) {
                        timeout = atoi(value);
                    } else if (counter > 1) {
                        break;
                    } 
                
                    value = strtok(NULL, DELIMITER);
                    counter++;
                }

                //send_nick(sockfd, player_nick);
                int len = strlen(player_nick);
                send_everything(sockfd, player_nick, &len);
            } break;

            default: {
                printf("Unknown action");
                exit(1);
            }
        }
        

        bzero(buffer, MAX_SIZE);
        //if (n <= 0) break;
        n = 0;
    }
}
   
int main (int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./client <NICK> [IP_ADDRESS] [PORT]\n");
        return 1;
    }

    if (argc < 3) {
        argv[2] = "127.0.0.1";
    }

    if (strlen(argv[1]) > PLAYER_MAX_NICK_LEN) {
        printf("Passed nick is too long\n");
        return 1;
    }

    signal(SIGTSTP, sighandler);
    signal(SIGINT, sig_int_handler);

    struct sockaddr_in servaddr, cli;
   
    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Socket creation failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));
   
    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    if (argc >= 4) {
        servaddr.sin_port = htons(atoi(argv[3]));
    } else {
        servaddr.sin_port = htons(PORT);
    }

    int error;
	if ((error = inet_pton(AF_INET, argv[2], &servaddr.sin_addr)) == -1) {
		printf("inet_pton error \n");
		exit(1);
	} else if (error = 0) { 
        printf("Addres error \n");
		exit(1);
	}
   
    // connect the client socket to server socket
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("Connection with the server failed...\n");
        exit(0);
    }
    else
        printf("Connected to the server..");
   
    game_loop(sockfd, argv[1]);
   
    // close the socket
    close(sockfd);
}