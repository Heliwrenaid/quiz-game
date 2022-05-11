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

// ---------------------------

struct game_config {
    struct questions * questions;
    int number_of_questions;
    int number_of_answers;
};

struct questions {
   char *text;
   char **answers;
   int correct_answer;
};


struct game_config 
get_game_config(const char* filename) {
    int number_of_questions = 0;
    int number_of_answers = 0;
    struct questions * questions;

    xmlDoc* document = xmlReadFile(filename, NULL, 0);

    xmlNode* root = xmlDocGetRootElement(document);
    xmlAttr* root_attr = root->properties;

    if (strcmp((char *)root_attr->name, "number_of_answers") == 0) {
        number_of_answers = *root_attr->children->content - 48;
    } else {
        printf("EXIT: Missed 'number_of_answers' attribute in root node\n");
        exit(1);
    }

    // count questions
    xmlNode* child = root->children->next;
    while (child != NULL) {
        if (strcmp(child->name, "question") == 0) {
            number_of_questions++;
        }
        else {
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
        (questions + i)->text = (char*) malloc(sizeof(node_attr->children->content) * sizeof(char));
        strcpy((questions + i)->text, node_attr->children->content);

        // get answers
        answer = node->children->next;
        char ** answers = (char**)malloc(number_of_answers * sizeof(char*));
        for (j = 0; j < number_of_answers; j++) {
            
            if (strcmp(answer->name, "answer") == 0) {
                answers[j] = (char*) malloc(sizeof(answer->children->content) * sizeof(char));
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

    return game_config;
}

// ---------------------------

void
display_game_config(struct game_config game_config) {
    struct questions * questions = game_config.questions;

    
    printf("\n--- Game configuration ---\n");
    printf("\nNumber of questions: %d\n", game_config.number_of_answers);
    printf("\nNumber of answers per question: %d\n", game_config.number_of_questions);

    printf("\nQuestions: \n");
    for(int i = 0; i < game_config.number_of_questions; i++) {
        printf("  [%d]\n", i + 1);
        printf("     Text: %s\n", (questions+i)->text);
        for (int k = 0; k < game_config.number_of_answers; k++) {
                printf("     Answer: %s\n", (questions+i)->answers[k]);
            }
        printf("     Correct answer:%d\n\n", (questions+i)->correct_answer);
    }
}

void
delete_game_config(struct game_config game_config) {
    free(game_config.questions);
    //TODO
}


int
main(void)
{
    bool game_started = false;
    struct game_config game_config = get_game_config("questions.xml");

    display_game_config(game_config);

    fd_set connection_fds;
    fd_set master;
    fd_set read_fds;
    struct sockaddr_in myaddr;
    struct sockaddr_in remoteaddr;
    int fdmax;
    int listener;
    int newfd;
    int nbytes;
    int yes = 1; // setsockopt(), SO_REUSEADDR
    int addrlen;
    int i, j;
    
    FD_ZERO(& connection_fds);
    FD_ZERO(& master);
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
    myaddr.sin_port = htons(PORT);
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
                        FD_SET(newfd, & master);
                        if(newfd > fdmax) {
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on socket %d\n",
                                inet_ntoa(remoteaddr.sin_addr), newfd);
                    }
                } else if(i == 0) {
                    printf("--- GAME STARTED ---\n");
                    game_started = true;
                    looping = false;
                    break;
                }
            }
        }
    }
    close(listener);
   
    for(;;) {
        read_fds = master;
        if(select(fdmax + 1, & read_fds, NULL, NULL, NULL) == - 1) {
            perror("select");
            exit(1);
        }
       
        for(i = 0; i <= fdmax; i++) {
            if(FD_ISSET( i, & read_fds)) {
               
            }
        }
    }
    
    delete_game_config(game_config);

    return 0;
}