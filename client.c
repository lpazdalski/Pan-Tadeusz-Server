#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <bits/siginfo.h>

typedef union {
    unsigned int value;
    struct
    {
        unsigned int A: 8;
        unsigned int B: 8;
        unsigned int C: 8;
        unsigned int D: 8;
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



void parseArguments(informations* info, int argc, char* argv[], int* pid, int* signalNumber,
                    int* bookNumber, int* interval, char** fragmentation, char** bulletinBoardPath);

//funkcja kodujaca odpowiedz do serwera
void encoder(informations* info, int signalNumber, int bookNumber, int interval, int fragmentation);

//inicjacja gniazda, wpisanie adresu z tablicy ogloszeniowej
void initSocket(int sfd, struct sockaddr_un* svaddr, struct sockaddr_un* claddr, char* sock_path);

//obluga otrzymywania oraz wysylania wiadomosci do serwera
void handleServer(int sfd, struct sockaddr_un* claddr, int interval);

//funkcja koduja ROT13
void rot13(char* message);


int main(int argc, char* argv[]) {

    int pid, bookNumber, interval, signalNumber;
    informations info;
    char* bulletinBoardPath;
    char* fragmentation;
    parseArguments(&info, argc, argv, &pid, &signalNumber, &bookNumber, &interval, &fragmentation, &bulletinBoardPath);
    reply rep;

    union sigval sv;
    sv.sival_int = info.value;

    struct sockaddr_un svaddr, claddr;

    sigset_t waitset;
    siginfo_t inf;

    sigfillset(&waitset);
    sigdelset(&waitset, SIGINT);
    sigprocmask(SIG_SETMASK, &waitset, NULL);

    if(sigqueue(pid, SIGRTMIN + 11, sv) == -1)
    {
        perror("sigqueue");
        exit(EXIT_FAILURE);
    }


    int result = sigwaitinfo(&waitset, &inf);
    if(result >= 0)
    {
        if(inf.si_signo == SIGRTMIN + signalNumber)
        {
            printf("sigwaitnfo returned for signal %d\n", inf.si_signo);
            rep.value = inf.si_value.sival_int;
            if(rep.confirmed == 1)
            {
                printf("Register rejected\n");
                exit(EXIT_FAILURE);
            }
            printf("length: %d\noffset: %d\n", rep.socketPathLength, rep.offset);
        }
    }
    else {
        printf("blad siqueue\n");
        perror("sigwait\n");
    }


    char socketPath[rep.socketPathLength];
    int fd = open(bulletinBoardPath, O_RDONLY);
    lseek(fd, rep.offset, SEEK_SET);
    read(fd, socketPath, rep.socketPathLength);
    close(fd);

    int sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if(sfd == -1)
        perror("socket");


    initSocket(sfd, &svaddr, &claddr, socketPath);
    handleServer(sfd, &svaddr, interval);

    remove(claddr.sun_path);
    remove(svaddr.sun_path);
    close(sfd);
    exit(EXIT_SUCCESS);


    return 0;
}

void parseArguments(informations* info, int argc, char* argv[], int* pid, int* signalNumber, int* bookNumber,
                    int* interval, char** fragmentation, char** bulletinBoardPath)
{
    int c;
    char* end;
    while((c = getopt(argc, argv, "s:r:x:f:o:p:")) != -1)
    {
        switch (c)
        {
            case 's':
                if((*pid = strtol( optarg, &end,0 )) <= 0L)
                {
                    fprintf(stderr, "failure PID parse\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'r':
                if((*signalNumber = strtol( optarg, &end,0 )) < 0 || *signalNumber > 30)
                {
                    fprintf(stderr, "signal number must be beetween 0 and 30\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'x':
                if((*bookNumber = strtol( optarg, &end,0 )) < 1 || *bookNumber > 12)
                {
                    fprintf(stderr, "book number must be beetween 1 and 12\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'f':
                (*fragmentation) = optarg;
                if(!strcmp(*fragmentation, "l" ) || !strcmp(*fragmentation, "z" ) || !strcmp(*fragmentation, "s" ));
                else
                {
                    fprintf(stderr, "fragmentation must be l, z or s\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'o':
                if((*interval = strtol( optarg, & end,0 )) < 0.0F)
                {
                    fprintf(stderr, "interval must be positive\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                *bulletinBoardPath = optarg;
                break;
            case '?':
                if (optopt == 's')
                    fprintf (stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr,
                             "Unknown option character.\n");
                exit(EXIT_FAILURE);
            default:
                printf("Usage: client -s <pid> -r <signal> -x <nr> -f <fragmentation> -o <interval> -p <time>\n");
                exit(EXIT_FAILURE);
        }
    }

    encoder(info, *signalNumber, *bookNumber, *interval, **fragmentation);

}

void encoder(informations* info, int signalNumber, int bookNumber, int interval, int fragmentation)
{
    //108 - l - podzial na wiersze
    //122 - z - podzial na litery
    //115 - s - podzial na slowa
    (*info).A = (unsigned int)signalNumber;
    (*info).B = (unsigned int)bookNumber;
    (*info).C = (unsigned int)fragmentation;
    (*info).D = (unsigned int)interval;
}

void initSocket(int sfd, struct sockaddr_un* svaddr, struct sockaddr_un* claddr, char* sock_path)
{

    memset(claddr, 0, sizeof(struct sockaddr_un));
    (*claddr).sun_family = AF_UNIX;
    snprintf((*claddr).sun_path,sizeof((*claddr).sun_path), "/tmp/cl.%ld", (long)getpid());


    memset(svaddr, 0, sizeof(struct sockaddr_un));
    (*svaddr).sun_family = AF_UNIX;
        for(int i=0 ; i< sizeof((*svaddr).sun_path); i++)
    {
        (*svaddr).sun_path[i] = sock_path[i];
    }

    if(bind(sfd, (struct sockaddr*) claddr, sizeof(struct sockaddr_un)) == -1)
        perror("bind");
}

void handleServer(int sfd, struct sockaddr_un* claddr, int interval)
{
    ssize_t  numBytes;
    socklen_t len;
    char resp[1024];
    char pid[5];
    int cPid = getpid();
    sprintf(pid, "%d", cPid);
    if (sendto(sfd, pid, 5, 0, (struct sockaddr *) claddr, sizeof(struct sockaddr_un)) < 0)
    {
        perror("sendTo");
        exit(EXIT_FAILURE);
    }
    struct timeval tv;
    int sec = interval/64 ;
    int usec = ((interval - (sec*64))/64.)*1000000;
    tv.tv_sec = sec + 1;
    tv.tv_usec = usec;

    while(1)
    {

        if (setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
            perror("zerwane polaczenie z serwerem");
            exit(EXIT_FAILURE);
        }

        len = sizeof(struct sockaddr_un);
        numBytes = recvfrom(sfd, resp, 1024, 0, NULL,NULL);

        if (numBytes == -1)
        {
            perror("recfrom");
            exit(EXIT_FAILURE);
        }


        if(resp[0] == EOF)
        {
            printf("Koniec streamu\n");
            exit(EXIT_SUCCESS);
        }

        printf("Response %.*s\n", (int) numBytes, resp);

        rot13(resp);

        if (sendto(sfd, resp, 1024, 0, (struct sockaddr *) claddr, len) < 0)
        {
            perror("sendTo");
            exit(EXIT_FAILURE);
        }
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