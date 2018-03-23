#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

typedef struct {
    int RTtimerNumber;
    int bookNumber;
    int fragmentation;
    int interval;
    int PID;
    FILE* fp;
    timer_t timerID;
    char* bookPath;
    char buff[1024];
    int socketfd;
    struct sockaddr_un claddr;

} client;

typedef union {
    unsigned int value;
    struct
    {
        unsigned int RTnumber: 8;
        unsigned int bookNumber: 8;
        unsigned int fragmentation: 8;
        unsigned int interval: 8;
    };
} informations;

typedef union {
    unsigned int value;
    struct
    {
        unsigned int confirmed: 8;
        unsigned int socketPathLength: 8;
        unsigned int offset: 16;
    };
} reply;

void parseArguments(int argc, char* argv[], char** panTadeuszPath, char** bulletinBoardPath);
int createSocket();

//funkcja kodujaca odpowiedz do klienta
reply encoder(int isAccepted, int socketPathLength, int offset);

//wygenerowanie z przestrzeni abstrakcyjnej adresu gniazda, wpisanie do tablicy ogloszeniowej oraz inicjacja gniazda
int initSockets(int sfd, struct sockaddr_un* svaddr, char* bulletinBoardPath, int* offset,
                int* socketPathLength, int client_PID);

//przypisanie stworzonym klientom numeru sygnalu obslugujacego timer oraz ustawienie deskrytporow gniazd klientow na -1
void initClients(client** cl);

//obsluga wybranej przez klienta fragmentacji ksiegi
void handleClient(client* cl, int* actualClients);

//wyslanie i odebranie komunikatu od klienta oraz sprawdzenie czy klient rozumie odbierane komunikaty
int sendAndRecv(int sfd, struct sockaddr_un claddr, char* row1, int interval);

//sprawdzenie czy klient jest prawdziwy
int checkClient(int sfd, struct sockaddr_un* claddr, int client_PID);

//wczytanie przeslanych informacji do struktury oraz otwarcie pliku do czytania wybranej ksiegi
void loadInformation(client* cl, siginfo_t i, char* panTadeuszPath);

//proces rejestracji klienta
void Register(client* cl, char* bulletinBoardPath, int replyRT, struct itimerspec* ts,
              struct sigevent* sev, timer_t* tidlist, int clientNumber);

//ustawenie budzika na zadany interwal
void setTimer(struct itimerspec* ts, struct sigevent* sev, int RTnumber, timer_t* tidlist, int interval, int clientNumber);

//funkcja kodujaca ROT13
void rot13(char* message);

//obsluga fragmentacji na slowa
int fragmentationWords(client* cl);

//obsluga fragmentacji na litery
int fragmentationLetters(client* cl);

//obsluga fragmentacji na wiersze
int fragmentationRows(client* cl);

//zwolnienie miejsca zajmowanego przez klienta
void deleteClient(client* cl, int* actualClients);

//zwolnienie pamieci
void freeClients(client** cl);

//obsluga sygnalow przychodzacych z budzikow oraz sygnalu rejestracji nowego klienta
void signalHandler(siginfo_t inf, int* actCl, client* clients, char* panTadeuszPath, char* bulletinBoardPath, struct itimerspec* ts,
                   struct sigevent* sev, timer_t* tidlist);

//wyslanie odrzucenia rejestracji
void refuseReply(int client_pid, int replyRT);

int main(int argc, char* argv[]) {

    char* panTadeuszPath;
    char* bulletinBoardPath;
    int sig;
    struct itimerspec ts;
    struct sigevent sev;
    timer_t* tidlist;

    parseArguments(argc, argv, &panTadeuszPath, &bulletinBoardPath);
	int fd = open(bulletinBoardPath,  O_CREAT |  O_WRONLY, 0777  );
	close(fd);

    printf("Server PID: %d\n", getpid());

    //maksymalna ilosc klientow - 30 ze wzgledu na 30 sygnalow RT, odejmujac SIRTMIN + 11
    tidlist = calloc(30, sizeof(timer_t));
    informations clientInformation;
    int actCl = 0;

    client* clients;
    initClients(&clients);

    sigset_t waitset;
    siginfo_t inf;

    sigfillset(&waitset);
    sigprocmask(SIG_SETMASK, &waitset, NULL);

    sig = sigwaitinfo(&waitset, &inf);
    if( sig >= 0)
    {
        clientInformation.value = inf.si_value.sival_int;
        loadInformation(&clients[0], inf, panTadeuszPath);
    }
    else
    {
        perror("sigwait\n");
        exit(EXIT_FAILURE);
    }


    Register(&clients[0], bulletinBoardPath, clientInformation.RTnumber, &ts,&sev,
                     tidlist, actCl);

    while(1)
    {
        sig = sigwaitinfo(&waitset, &inf);
        if( sig >= 0)
        {
            signalHandler(inf, &actCl, clients, panTadeuszPath, bulletinBoardPath, &ts, &sev, tidlist);
        }
        else
        {
            perror("sigwait\n");
        }
    }

    return 0;
}

void signalHandler(siginfo_t inf, int* actCl, client* clients, char* panTadeuszPath, char* bulletinBoardPath, struct itimerspec* ts,
                   struct sigevent* sev, timer_t* tidlist)
{
    informations clientInformation;
    if(inf.si_signo == SIGRTMIN + 11)
    {
        clientInformation.value = inf.si_value.sival_int;
        if(*actCl == 29)
        {
            printf("nie ma miejsca na kolejnego klienta");
            refuseReply(inf.si_pid, clientInformation.RTnumber);
            return;
        }
        else
        {
            for(int i=0; i < 30; i++)
            {
                if (clients[i].socketfd == -1)
                {
                    (*actCl)++;
                    loadInformation(&clients[i], inf, panTadeuszPath);
                    Register(&clients[i], bulletinBoardPath, clientInformation.RTnumber, ts,sev,
                             tidlist, i );
                    printf("Dodano nowego klienta\n");
                    return;
                }
            }
        }
    }
    else if(inf.si_signo == SIGINT)
    {
        freeClients(&clients);
        for(int i=0 ; i< 30; i++)
        {
            close(clients[i].socketfd);
        }
        exit(1);
    }
    else
    {
        for(int i=0; i < 30; i++)
        {
            if (inf.si_signo == clients[i].RTtimerNumber)
            {
                handleClient(&clients[i], actCl);
                return;
            }
        }
    }
}

void parseArguments(int argc, char* argv[], char** panTadeuszPath, char** bulletinBoardPath)
{
    int c;
    while((c = getopt(argc, argv, "k:p:")) != -1)
    {
        switch (c)
        {
            case 'k':
                *panTadeuszPath = optarg;
                break;
            case 'p':
                *bulletinBoardPath = optarg;
                break;
            case '?':
                if (optopt == 'k')
                    fprintf (stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr,
                             "Unknown option character.\n");
                exit(EXIT_FAILURE);
            default:
                printf("Usage: rytmika -k <path> -p <path>\n");
                exit(EXIT_FAILURE);
        }
    }
}


int createSocket()
{
    int sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if(sfd == -1)
        perror("socket");
    return sfd;
}

reply encoder(int isAccepted, int socketPathLength, int offset)
{
    reply i;
    i.confirmed = isAccepted;
    i.socketPathLength = socketPathLength;
    i.offset = offset;
    return i;
}

int initSockets(int sfd, struct sockaddr_un* svaddr, char* bulletinBoardPath, int* offset,
                int* socketPathLength, int client_PID)
{
    static char sC = 'a';
    memset(svaddr, 0, sizeof(struct sockaddr_un));
    (*svaddr).sun_family = AF_UNIX;

    (*svaddr).sun_path[0] = '\0';
    char tmp[5];
    snprintf(tmp,5, "%d", client_PID);
    for(int i=1; i< sizeof(tmp); i++ )
        (*svaddr).sun_path[i] = tmp[i-1];
    (*svaddr).sun_path[7] = sC++;

    int fd = open(bulletinBoardPath, O_RDWR  );
    *offset = lseek(fd, 0, SEEK_END); //przesuwam na koniec tablicy ogloszeniowej
    *socketPathLength = sizeof((*svaddr).sun_path);
    write(fd, (*svaddr).sun_path,  *socketPathLength);
    close(fd);
	remove(svaddr->sun_path);
    if(bind(sfd, (struct sockaddr*) svaddr, sizeof(struct sockaddr_un)) == -1)
    {
        perror("bind");
        return 0;
    }
    return 1;
}

void initClients(client** cl)
{
    *cl = calloc(30, sizeof(client));
    for(int i = 0, k = 0; i < 30; i++, k++)
    {
        (*cl)[i].socketfd = -1;
        if(k == 11) k++;
        (*cl)[i].RTtimerNumber = SIGRTMIN + k;
    }
}

void handleClient(client* cl, int* actualClients)
{
    int result = 0;
    if((*cl).fragmentation == 'l')
    {
        result = fragmentationRows(cl);
    }
    else if ((*cl).fragmentation == 'z')
    {
        result = fragmentationLetters(cl);
    }
    else if((*cl).fragmentation == 's')
    {
        result = fragmentationWords(cl);
    }

    if(!result)
    {
        deleteClient(cl, actualClients);
    }
}

int checkClient(int sfd, struct sockaddr_un* claddr, int client_PID)
{
    ssize_t  numBytes;
    char buf[5];
    socklen_t len = sizeof(struct sockaddr_un);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    if (setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("zerwane polaczenie z klientem");
        return 0;
    }

    numBytes = recvfrom(sfd, buf, 5, 0, (struct sockaddr *) claddr, &len);
    if (numBytes == -1)
    {
        perror("recfrom");
        return 0;
    }

    int check = strtol( buf,NULL,0);
    if (check != client_PID)
        return 0;
    return 1;
}

int sendAndRecv(int sfd, struct sockaddr_un claddr, char* row1, int interval)
{
    struct timeval tv;
    int sec = interval/64 ;
    int usec = ((interval - (sec*64))/64.)*1000000;
    tv.tv_sec = sec;
    tv.tv_usec = usec;


    ssize_t  numBytes;
    char buff[1024];
    if (sendto(sfd, row1, 1024 , 0, (struct sockaddr *) &claddr, sizeof(struct sockaddr_un)) < 0)
    {
        perror("zerwane polaczenie z klientem");
        return 0;
    }
    rot13(row1);

    if (setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("zerwane polaczenie z klientem");
        return 0;
    }

    numBytes = recvfrom(sfd, buff, 1024, 0, NULL, NULL);
    if (numBytes == -1)
    {
        perror("recfrom");
        return 0;
    }

    if(strcmp(buff, row1) != 0 )
    {
        printf("klient nie rozumie tekstu\n");
        return 0;
    }

    return 1;
}

void loadInformation(client* cl, siginfo_t i, char* panTadeuszPath)
{
    informations clientInformation;
    clientInformation.value = i.si_value.sival_int;
    (*cl).PID = i.si_pid;
    (*cl).bookNumber = clientInformation.bookNumber;
    (*cl).interval = clientInformation.interval;
    (*cl).fragmentation = clientInformation.fragmentation;
    char bookPath[strlen(panTadeuszPath) + 9];
    char bookNumber[2];
    snprintf(bookNumber,2, "%d", clientInformation.bookNumber);
    strcpy(bookPath, panTadeuszPath);
    strcat(bookPath, "/ksiega");
    strcat(bookPath, bookNumber);
    (*cl).bookPath = bookPath;
    FILE *fp = fopen(bookPath, "r");
    if (fp == NULL)
        exit(1);
    (*cl).fp = fp;
}

void Register(client* cl, char* bulletinBoardPath, int replyRT, struct itimerspec* ts,
              struct sigevent* sev, timer_t* tidlist, int clientNumber)
{
    struct sockaddr_un svaddr;

    (*cl).socketfd = createSocket();

    int offset, socketPathLength;

    if(!initSockets( (*cl).socketfd, &svaddr, bulletinBoardPath, &offset, &socketPathLength, (*cl).PID))
    {
        close((*cl).socketfd);
        (*cl).socketfd = -1;
        return;
    }

    union sigval sv;
    reply i = encoder(0, socketPathLength, offset);
    sv.sival_int = i.value;
    printf("replyRT: SIRTMIN + %d Client PID: %d\n", replyRT,(*cl).PID);
    if(sigqueue( (*cl).PID, SIGRTMIN + replyRT, sv) == -1)
    {
        perror("sigqueue");
        return;
    }

    if(!checkClient((*cl).socketfd, &(*cl).claddr, (*cl).PID))
    {
        close((*cl).socketfd);
        (*cl).socketfd = -1;
        return;
    }

    setTimer(ts,sev, (*cl).RTtimerNumber, tidlist, (*cl).interval,clientNumber);
    if(timer_create(CLOCK_REALTIME, sev, &tidlist[clientNumber]) == -1)
        printf("blad tworzenia timera\n");
    if(timer_settime(tidlist[clientNumber], 0, ts, NULL) == -1)
        perror("blad nastawienia timera\n");
    (*cl).timerID = tidlist[clientNumber];
}

void setTimer(struct itimerspec* ts, struct sigevent* sev, int RTnumber, timer_t* tidlist, int interval, int clientNumber)
{
    (*sev).sigev_notify = SIGEV_SIGNAL;
    (*sev).sigev_signo = RTnumber;

    int sec = interval/64;
    int nsec = ((interval - (sec*64))/64.)*1000000000;

    (*ts).it_interval.tv_sec = sec;
    (*ts).it_interval.tv_nsec = nsec;
    (*ts).it_value.tv_sec = sec;
    (*ts).it_value.tv_nsec = nsec;
}

int fragmentationWords(client* cl)
{
    char c;
    int result = 0, i = 0;
    if((c = fgetc((*cl).fp)) != EOF)
    {
        if(c == '\n')
        {
            (*cl).buff[i++] = '\n';
            (*cl).buff[i] = '\0';
            result = sendAndRecv((*cl).socketfd, (*cl).claddr, (*cl).buff, (*cl).interval);
        }
        else
        {
            while (c != '\n' && c != ' ')
            {
                (*cl).buff[i++] = c;
                c = fgetc((*cl).fp);
                if(c == '\n')
                    ungetc(c,(*cl).fp );
            }
            (*cl).buff[i] = '\0';
            result = sendAndRecv((*cl).socketfd, (*cl).claddr, (*cl).buff, (*cl).interval);
        }
    }
    else
    {
        char buff[1] = {EOF};
        if (sendto((*cl).socketfd, buff, 1 , 0, (struct sockaddr *) &(*cl).claddr, sizeof(struct sockaddr_un)) < 0)
        {
            perror("zerwane polaczenie z klientem");
            return 0;
        }
        result = 0;
    }
    return result;
}

int fragmentationLetters(client* cl)
{
    char buf[1];
    int result = 0;
    if((buf[0] = fgetc((*cl).fp)) != EOF)
    {
        if(buf[0] == ' ' || buf[0] == '\n')
        {
            while((buf[0] = fgetc((*cl).fp)) == ' ' || buf[0] == '\n');
        }
        result = sendAndRecv((*cl).socketfd, (*cl).claddr, buf, (*cl).interval);
    }
    else{
        char buff[1] = {EOF};
        if (sendto((*cl).socketfd, buff, 1 , 0, (struct sockaddr *) &(*cl).claddr, sizeof(struct sockaddr_un)) < 0)
        {
            perror("zerwane polaczenie z klientem");
            return 0;
        }
        result = 0;
    }
    return result;
}

int fragmentationRows(client* cl)
{
    int result = 0;
    if (fgets((*cl).buff, sizeof((*cl).buff), (*cl).fp) != NULL)
    {
        result = sendAndRecv((*cl).socketfd, (*cl).claddr, (*cl).buff, (*cl).interval);
    }
    else{
        char buff[1] = {EOF};
        if (sendto((*cl).socketfd, buff, 1 , 0, (struct sockaddr *) &(*cl).claddr, sizeof(struct sockaddr_un)) < 0)
        {
            perror("zerwane polaczenie z klientem");
            return 0;
        }
        result = 0;
    }
    return result;
}

void deleteClient(client* cl, int* actualClients)
{
    timer_delete((*cl).timerID);
    fclose((*cl).fp);
    close((*cl).socketfd);
    (*cl).socketfd = -1;
    (*actualClients)--;
}

void freeClients(client** cl)
{
    free(*cl);
}

void refuseReply(int client_pid, int replyRT)
{
    union sigval sv;
    reply i = encoder(1,0, 0);
    sv.sival_int = i.value;
    if(sigqueue( client_pid, SIGRTMIN + replyRT, sv) == -1)
    {
        perror("sigqueue");
        exit(EXIT_FAILURE);
    }
}

void rot13(char* message)
{
    int i = 0;
    int off = 0;
    for(i = 0; message && message[i]; ++i)
    {
        if(message[i] < 'a' || (message)[i] > 'z') continue;

        for(off = 13; off > ('z' - message[i]); )
        {
            off -= (1 + 'z' - message[i]);
            message[i] = 'a';
        }
        message[i] += off;
    }

    for(i = 0; message && message[i]; ++i)
    {
        if(message[i] < 'A' || (message)[i] > 'Z') continue;

        for(off = 13; off > ('Z' - message[i]); )
        {
            off -= (1 + 'Z' - message[i]);
            message[i] = 'A';
        }
        message[i] += off;
    }
}