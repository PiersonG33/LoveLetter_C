
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#define DECK_SIZE 21
struct Card
{
    char *name;
    int value;
};
struct Player
{
    struct Card hand;
    long unsigned int threadID;
    int turn;
};

extern int total_guesses; // total number of Valid guesses
extern int total_wins;    // total wins
extern int total_losses;  // total losses-- these are *every* loss

struct Card* deck; // deck of cards
int deckSize;
struct Card* discard; // discard pile
int discardSize;
struct Card aside; // the one card set aside at the start of the game
struct Player* players; // array of players
int turn; // current turn

pthread_t *tids; // array of thread ids
int numTids; // number of threads
int numPlayers; 
pthread_mutex_t mutex1; //drawing a card
pthread_mutex_t mutex2; //
pthread_mutex_t mutex3;


volatile sig_atomic_t shutDownNow = 0;

int compareString(char *word1, char *word2, int length)
{
    for (int i = 0; i < length; i++)
    {
        if (tolower(*(word1 + i)) != tolower(*(word2 + i)))
        {
            return 0;
        }
    }
    return 1; // If we get here, the strings are equal
}

int compareCard(struct Card card1, struct Card card2)
{
    if (card1.value == card2.value)
    {
        return 0;
    }
    else if (card1.value > card2.value)
    {
        return 1;
    }
    else
    {
        return -1;
    }
}

void swap(struct Card *card1, struct Card *card2)
{
    struct Card temp = *card1;
    *card1 = *card2;
    *card2 = temp;
}

void shuffleDeck(struct Card* deck, int size){
    for (int i = 0; i < size; i++){
        int randomIndex = rand() % size;
        swap(deck + i, deck + randomIndex);
    }
}

void sigusr1_handler(int signum)
{
    shutDownNow = 1; //When the SIGUSR1 signal is received, shutDownNow is set to 1
    //Terminate child threads, free dynamic memory, etc
}


void* clienthandler(void *socket_desc){

    //game. 
    int client_socket = *(int*)socket_desc;
    //free(socket_desc);

    struct Player* self = NULL;
    for (int i = 0; i < numPlayers; i++){
        if ((players + i)->threadID == pthread_self()){
            self = (players + i);
            break;
        }
    }
    int ownTurn = self->turn; //Unchanging

    while(1){//Client control loop

        char* buffer = (char*)calloc(6, sizeof(char));
        ssize_t bytesIn = recv(client_socket, buffer, 5, 0);
        if (bytesIn <= 0){
            //Client disconnected
            printf("THREAD %lu: client gave up; closing TCP connection...\n", pthread_self());
            free(buffer);
            break;
        }
        if (turn == 0){
            // do nothing, game has not yet started
        }
        *(buffer + 5) = '\0'; //Gotta end the string
        printf("THEAD %lu: rcvd guesses\n", pthread_self());

        char* packageToSend = (char*)calloc(8, sizeof(char));

        
        send(client_socket, packageToSend, 8, 0); //Send the packet
        
        free(packageToSend);

        free(buffer);
    }

    close(client_socket);
    pthread_exit(NULL);
}

int letter_server(int argc, char ** argv)
{
    //This program is designd to simulate a game of Love Letter

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc != 5)
    {
        printf("ERROR: Invalid argument(s)\nUSAGE: hw3.out <listener-port> <seed> <number of players>\n"); 
        return EXIT_FAILURE; 
    }

    int port = atoi(*(argv + 1));
    int seed = atoi(*(argv + 2));
    numPlayers = atoi(*(argv + 3));
    
    tids = (pthread_t*)calloc(numPlayers, sizeof(pthread_t)); //1 by default
    players = (struct Player*)calloc(numPlayers, sizeof(struct Player));
    numTids = 0;

    if (seed == -1){
        srand(time(NULL)); //-1 for true random
    }
    else{
        srand(seed);
    }

    printf("MAIN: seeded pseudo-random number generator with %d\n", seed);

    deck = (struct Card*)calloc(DECK_SIZE, sizeof(struct Card));
    int counter = 0;
    for (int i = 0; i < 6; i++){
        (deck + counter)->name = "Guard";
        (deck + counter)->value = 1;
        counter++;
    }
    for (int i = 0; i < 2; i++){
        (deck + counter)->name = "Spy";
        (deck + counter)->value = 0;

        (deck + counter+2)->name = "Priest";
        (deck + counter+2)->value = 2;

        (deck + counter+4)->name = "Baron";
        (deck + counter+4)->value = 3;

        (deck + counter+6)->name = "Handmaid";
        (deck + counter+6)->value = 4;

        (deck + counter+8)->name = "Prince";
        (deck + counter+8)->value = 5;

        (deck + counter+10)->name = "Chancellor";
        (deck + counter+10)->value = 6;
        counter++;
    }
    (deck + counter+18)->name = "King";
    (deck + counter+18)->value = 7;
    (deck + counter+19)->name = "Countess";
    (deck + counter+19)->value = 8;
    (deck + counter+20)->name = "Princess";
    (deck + counter+20)->value = 9;
    shuffleDeck(deck, DECK_SIZE);

    printf("MAIN: Server listening on port {%d}\n", port); 


    // make socket, check if it worked.

    pthread_t id;
    int client_sock, c;

    struct sockaddr_in server, client;

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1)
    {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port); //Changed this to be the port number passed in as an argument

    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        // print the error message
        perror("bind failed. Error");
        return 1;
    }

    if (listen(sd, 50) == -1)
    {
        perror("listen() failed");
        return EXIT_FAILURE;
    }

    c = sizeof(struct sockaddr_in);

    signal(SIGUSR1, sigusr1_handler); //Signal handling for termination signal

    while (shutDownNow == 0)
    {
        //we have accepted a connection!
        client_sock = accept(sd, (struct sockaddr *)&client, (socklen_t *)&c);
        if (shutDownNow == 1){
            printf("MAIN: SIGUSR1 rcvd; Server shutting down...\n");
            //shut down the server
            //free all dynamic memory
            //terminate all child threads
            return EXIT_SUCCESS;
            break;
        }
        if (client_sock < 0)
        {
            perror("accept failed");
            return 1;
        }
        if (numTids == numPlayers){
            printf("MAIN: max players reached; closing TCP connection...\n");
            aside = *(deck + deckSize - 1);
            deckSize--;
            deck = (struct Card*)realloc(deck, sizeof(struct Card) * deckSize);
            close(client_sock);
            continue;
        }
        if( pthread_create( &id , NULL ,  clienthandler , (void*) &client_sock) < 0)
        {
            perror("could not create thread");
            return 1;
        }
        printf("MAIN: rcvd incoming connection request\n");
        (players + numTids)->threadID = id;
        (players + numTids)->turn = numTids + 1;
        (players + numTids)->hand = *(deck + deckSize - 1); //Give the player a card
        deckSize--;
        deck = (struct Card*)realloc(deck, sizeof(struct Card) * deckSize);
        *(tids + numTids) = id;
        numTids++;
        //tids = (pthread_t*)realloc(tids, sizeof(pthread_t) * (numTids + 1));
        //Array now holds all the ids, although realloc every time is probably not the best way to do this
        pthread_detach(id); //Detach the thread so it can be joined later (never)

    }

    

    return EXIT_SUCCESS;
}
