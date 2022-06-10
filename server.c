#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include <libxml/parser.h>

#define PORT 9034
#define DELIMITER "##"
#define ANSWER_TIME 10
#define MAX_SIZE 1024
#define MAX_NUMBER_OF_PLAYERS 30

#define BASIC_POINTS 10
#define EXTRA_POINTS 15

#define TIMEOUT 15

#define PLAYER_NUM_OFFSET 3
#define PLAYER_MAX_NICK_LEN 15

// ---------------------------

struct game_config {
    struct questions * questions;
    int number_of_questions;
    int number_of_answers;
    int timeout;
};

struct questions {
   char *text;
   char **answers;
   int correct_answer;
};

struct player_data {
    int sockfd;
    int points;
    bool received_data;
    bool sended_data;
    char * nick;
};


struct game_config 
get_game_config(const char* filename) {
    int number_of_questions = 0;
    int number_of_answers = 0;
    int timeout = 0;
    struct questions * questions;

    xmlDoc* document = xmlReadFile(filename, NULL, 0);

    xmlNode* root = xmlDocGetRootElement(document);
    xmlAttr* attribute = root->properties;
    
    while (attribute) {
        if (strcmp((char *)attribute->name, "number_of_answers") == 0) {
            number_of_answers = atoi(attribute->children->content);
        } else if (strcmp((char *)attribute->name, "timeout") == 0) {
            timeout = atoi(attribute->children->content);
        } else {
            printf("EXIT: Missed 'number_of_answers' attribute in root node\n");
            exit(1);
        }
        attribute = attribute->next;
    }

    // count questions
    xmlNode* child = root->children->next;
    while (child != NULL) {
        if (strcmp(child->name, "question") == 0) {
            number_of_questions++;
        } else {
            printf("EXIT: Unknown tag\n");
            exit(1);
        }
        child = child->next->next;
    }
    questions = (struct questions*) malloc(number_of_questions * sizeof(struct questions));

    // go through the questions
    xmlNode* node = root->children->next;
    xmlNode* answer;
    xmlAttr* node_attr;
    xmlAttr* answer_attr;
    int i, j;
    for (i = 0; i < number_of_questions; i++) {
        node_attr = node->properties;
        (questions + i)->text = (char*) malloc((strlen(node_attr->children->content) + 1) * sizeof(char));
        strcpy((questions + i)->text, node_attr->children->content);
        // get answers
        answer = node->children->next;
        char ** answers = (char**)malloc(number_of_answers * sizeof(char*));
        for (j = 0; j < number_of_answers; j++) {
            
            if (strcmp(answer->name, "answer") == 0) {
                printf("%s: %d\n", answer->children->content, strlen(answer->children->content));
                answers[j] = (char*) malloc((strlen(answer->children->content) + 1) * sizeof(char));
                strcpy(answers[j], answer->children->content);
                
                //check if it is correct answer
                answer_attr = answer->properties;
                if (answer_attr != NULL) {
                    if (strcmp(answer_attr->name, "correct") == 0) {
                        (questions+i)->correct_answer = j;
                    } else {
                        printf("EXIT: Unknown answer attribute\n");
                        exit(1);
                    }
                }
               
            } else {
                printf("EXIT: Unknown tag\n");
                exit(1);
            }
            answer = answer->next->next;
        }
        (questions+i)->answers = answers;

        // move to the next question
        node = node->next->next;
    }
    

    // clean up
    xmlFreeDoc(document);

    struct game_config game_config;
    game_config.questions = (struct questions*) malloc(number_of_questions * sizeof(struct questions));
    game_config.questions = questions;
    game_config.number_of_answers = number_of_answers;
    game_config.number_of_questions = number_of_questions;
    if (timeout == 0) timeout = TIMEOUT;
    game_config.timeout = timeout;
    return game_config;
}

// ---------------------------

void
display_game_config(struct game_config game_config) {
    struct questions * questions = game_config.questions;

    
    printf("\n--- Game configuration ---\n");
    printf("\nNumber of questions: %d\n", game_config.number_of_questions);
    printf("\nNumber of answers per question: %d\n", game_config.number_of_answers);
    printf("\nTimeout: %d\n", game_config.timeout);

    printf("\nQuestions: \n");
    for(int i = 0; i < game_config.number_of_questions; i++) {
        printf("  [%d]\n", i + 1);
        printf("     Text: %s\n", (questions+i)->text);
        for (int k = 0; k < game_config.number_of_answers; k++) {
                printf("     Answer: %s\n", (questions+i)->answers[k]);
            }
        printf("     Correct answer: %d\n\n", (questions+i)->correct_answer);
    }
}

void
delete_game_config(struct game_config game_config) {
    free(game_config.questions);
    //TODO
}


int 
send_everything(int sockfd, char * buffer, int * len)
{
    int total = 0; // number of sended bytes
    int bytesleft = * len;
    int n;
   
    while(total < * len) {
        n = send(sockfd, buffer + total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }
   
    * len = total;
   
    return n == -1 ? -1: 0;
}

char *
serialize_question(struct questions question, int number_of_answers) {

    char * buffer = (char*) malloc(MAX_SIZE);
    strcpy(buffer, DELIMITER);
    strcat(buffer, "question");
    strcat(buffer, DELIMITER);
    strcat(buffer, question.text);
    int i;
    for (i = 0; i < number_of_answers; i++) {
        strcat(buffer, DELIMITER);
        strcat(buffer, question.answers[i]);
    }
    return buffer;
}

int
send_question(int sockfd, struct questions question, int number_of_answers) {
    char * buffer = serialize_question(question, number_of_answers);
    int len = strlen(buffer);
    return send_everything(sockfd, buffer, &len);
}

int
parse_answer(char * message) {
    return strtol(message, NULL, 10);
}

bool
can_go_to_next_question(struct player_data * players, int number_of_players) { // TODO: timeout
    int i;
    for (i = 0; i < number_of_players; i++) {
        if (players[i].sockfd == -1) {
            continue;
        }
        if (players[i].sended_data == false || players[i].received_data == false) {
            return false;
        }
    }
    return true;
}

void 
players_reset_flags(struct player_data * players, int number_of_players) {
    int i;
    for (i = 0; i < number_of_players; i++) {
        players[i].received_data = false;
        players[i].sended_data = false;
    }
}

void
debug_players(struct player_data * players, int number_of_players) {
     int i;
    for (i = 0; i < number_of_players; i++) {
        printf("\n------\n");
        printf("Player %d: sockfd: %d\n", i, players[i].sockfd);
        printf("Player %d: recieved: %i\n", i, players[i].received_data);
        printf("Player %d: sended: %i\n", i, players[i].sended_data);
        printf("\n------\n");
    }
}

char *
parse_nick(char * value) {
    int counter = -1;
    while (value != NULL) {
        if (counter == 0) {
            printf("Question: %s\n", value);
        } else if (counter > 0) {
            printf("  [%d] %s\n", counter, value);
        }
        
        value = strtok(NULL, DELIMITER);
        counter++;
    } 
}

char * 
serialize_results(int sockfd, struct player_data * players, int number_of_players) {

    // ##result##<nick>##<points>##<nick>##<points>##...

    char * buffer = (char*) malloc(MAX_SIZE);
    strcpy(buffer, DELIMITER);
    strcat(buffer, "result");
    char string[15];

    int i;
    for (i = 0; i < number_of_players; i++) {
        if (players[i].sockfd == -1) continue;

        strcat(buffer, DELIMITER);
        strcat(buffer, players[i].nick);

        if (sockfd == players[i].sockfd) {
            strcat(buffer, "(YOU)");
        }

        strcat(buffer, DELIMITER);
        sprintf(string,"%d", players[i].points);
        strcat(buffer, string);
    }
    
    return buffer;
}

int
send_results(int sockfd, struct player_data * players, int number_of_players) {
    char * buffer = serialize_results(sockfd, players, number_of_players);
    int len = strlen(buffer);
    return send_everything(sockfd, buffer, &len);
}

char *
serialize_configuration(struct game_config game_config) {

    char * buffer = (char*) malloc(MAX_SIZE);
    char string[15];

    strcpy(buffer, DELIMITER);
    strcat(buffer, "config");
    strcat(buffer, DELIMITER);

    sprintf(string,"%d", game_config.number_of_answers);
    strcat(buffer, string);
    strcat(buffer, DELIMITER);
    
    sprintf(string,"%d", game_config.timeout);
    strcat(buffer, string);
   
    return buffer;
}

int
send_configuration(int sockfd, struct game_config game_config) {
    char * buffer = serialize_configuration(game_config);
    int len = strlen(buffer);
    return send_everything(sockfd, buffer, &len);
}

bool
any_player_playing(struct player_data * players, int number_of_players) {
    int i;
    for (i = 0; i < number_of_players; i++) {
        if (players[i].sockfd != -1) {
            return true;
        }
    }
    return false;
}

int
main(int argc, char *argv[]) {
    bool game_started = false;
    struct game_config game_config = get_game_config("questions.xml");
    struct player_data * players = (struct player_data *) calloc(MAX_NUMBER_OF_PLAYERS, sizeof(struct player_data));
    display_game_config(game_config);

    fd_set connection_fds; // used when players connect to server
    fd_set players_fds;
    fd_set read_fds;
    struct sockaddr_in myaddr;
    struct sockaddr_in remoteaddr;
    int number_of_players;
    int fdmax;
    int listener;
    int newfd;
    int nbytes;
    int yes = 1; // setsockopt(), SO_REUSEADDR
    int addrlen;
    int i, j;
    
    FD_ZERO(& connection_fds);
    FD_ZERO(& players_fds);
    FD_ZERO(& read_fds);

    if((listener = socket(AF_INET, SOCK_STREAM, 0)) == - 1) {
        perror("socket");
        exit(1);
    }
   
    if(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, & yes,
    sizeof(int)) == - 1) {
        perror("setsockopt");
        exit(1);
    }
   
    // bind
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = INADDR_ANY;
    if (argc >= 2) {
        myaddr.sin_port = htons(atoi(argv[1]));
    } else {
        myaddr.sin_port = htons(PORT);
    }

    memset(& (myaddr.sin_zero), '\0', 8);
    if(bind(listener,(struct sockaddr *) & myaddr, sizeof(myaddr)) == - 1) {
        perror("bind");
        exit(1);
    }
   
    // listen
    if(listen(listener, 10) == - 1) {
        perror("listen");
        exit(1);
    }

    FD_SET(0, & connection_fds);
    FD_SET(listener, & connection_fds);
    fdmax = listener;

    bool looping = true;

    printf("--- Waiting for clients ... ---\n");
    printf("Listen on port: %d\n",ntohs(myaddr.sin_port));
    printf("Press ENTER to start the game\n");

    while(looping) {
        read_fds = connection_fds;
        if(select(listener + 1, & read_fds, NULL, NULL, NULL) == - 1) {
            perror("select");
            exit(1);
        }

        for(i = 0; i <= listener; i++) {
            if(FD_ISSET(i, & read_fds)) {
                if(i == listener && game_started == false) {
                    // accept new connections
                    addrlen = sizeof(remoteaddr);
                    if((newfd = accept(listener,(struct sockaddr *) & remoteaddr,
                    & addrlen)) == - 1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, & players_fds);
                        if(newfd > fdmax) {
                            fdmax = newfd;
                        }
                        players[number_of_players].sockfd = newfd;
                        players[number_of_players].nick = (char *) malloc(PLAYER_MAX_NICK_LEN * sizeof(char));
                        number_of_players++;

                        if (number_of_players >= MAX_NUMBER_OF_PLAYERS) {
                            printf("-----\nWarning: maximum number of players joined: %d\n-----\n", MAX_NUMBER_OF_PLAYERS);
                            printf("--- GAME SETUP PHASE ---\n");
                            game_started = true;
                            looping = false;
                            break;
                        }
                        
                        printf("selectserver: new connection from %s on socket %d\n",
                                inet_ntoa(remoteaddr.sin_addr), newfd);
                    }
                } else if(i == 0) {
                    printf("--- GAME SETUP PHASE ---\n");
                    game_started = true;
                    looping = false;
                    break;
                }
            }
        }
    }
    close(listener);

    int n;
    int player_fd;

    // configuration phase
    char player_nick[PLAYER_MAX_NICK_LEN];
    for (i = 0; i < number_of_players; i++) {
        player_fd = players[i].sockfd;

        send_configuration(player_fd, game_config);

        if (n = recv(player_fd, player_nick, sizeof(player_nick), 0) <= 0) {
            printf("Player on socket %d is disconnected\n", player_fd);
            FD_CLR(player_fd, &players_fds);
            players[i].sockfd = -1;
            close(player_fd);
        } else {
            printf("Received answer %s on socket %d\n", player_nick, player_fd);
            strcpy(players[i].nick, player_nick);
        }

        memset(player_nick, '\0', sizeof(char)*PLAYER_MAX_NICK_LEN);
    }

    char buffer[11];
    int question_number = 0;
    bool is_first_answer = true;
    int answer;


    printf("\n--- GAME PHASE ---\n");
    while(any_player_playing(players, number_of_players)) {

        for(i = 0; i < number_of_players; i++) {
            player_fd = players[i].sockfd;

            if(FD_ISSET(player_fd, & players_fds) && !players[i].sended_data) {
                printf("Sending question number %d to player %s\n", question_number, players[i].nick);
                if(send_question(player_fd, game_config.questions[question_number], game_config.number_of_answers) == - 1) {
                    perror( "sendall" );
                } else {
                    players[i].sended_data = true;
                }
            }
        }

        read_fds = players_fds;
        if(select(fdmax + 1, & read_fds, NULL, NULL, NULL) == - 1) {
            perror("select");
            exit(1);
        }
       
        for(i = 0; i < number_of_players; i++) {

            player_fd = players[i].sockfd;

            if(FD_ISSET(player_fd, & read_fds) && !players[i].received_data) {
               
                if (n = recv(player_fd, buffer, sizeof(buffer), 0) <= 0){
                    printf("Player %s is disconnected\n", players[i].nick);
                    FD_CLR(player_fd, &players_fds);
                    players[i].sockfd = -1;
                    close(player_fd);
                } else {
                    printf("Received answer %s from player %s\n", buffer, players[i].nick);

                    players[i].received_data = true;

                    // calculate user statistics ---------
                    answer = parse_answer(buffer);
                    if (answer == game_config.questions[question_number].correct_answer) {
                        if (is_first_answer) {
                            players[i].points += EXTRA_POINTS;
                            is_first_answer = false;
                        } else {
                            players[i].points += BASIC_POINTS;
                        }
                    }

                }
            }
        }
        

        if (can_go_to_next_question(players, number_of_players)) {
            if (question_number < game_config.number_of_questions - 1) {
                question_number++;
                players_reset_flags(players, number_of_players);
                is_first_answer = true;
            } else {
                break;
            }
        }
    }

    printf("\n--- GAME IS ENDED ---\n");
    
    //int i;
    for (i = 0; i < number_of_players; i++) {
        if (players[i].sockfd != -1) {
            printf("Sending result to %s: %d\n", players[i].nick, players[i].points);
            send_results(players[i].sockfd, players, number_of_players);
        }
    }

    delete_game_config(game_config);

    return 0;
}