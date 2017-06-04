#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <sys/sem.h>

#include <pthread.h>

#define LIMIT 20

//tags:
#define CLOCK 111 //Lamport's clock
#define DREQ 222 //House request
#define DACK 333 //House acknowledge
#define KMREQ 1025 //Kasprzak and fog request
#define KMACK 1026 //Kasprzak and fog acknowledge
#define PREQ 2049
#define PACK 2050

bool stop = false;

int di,dj;
int pstat = 0;
/*
status grupy egzorcystow
0    nic nie chca
1    chca dom
2    otrzymali dom
3    nie dostana danego domu
4    chca kasprzaga i sprzęt do mgły
5    otrzymali kasprzaga i mgłę
6    chcą prześcieradła
7    otrzymali prześcieradła
*/

int size,pid;
int lclock = 0;
int clockSem;

int *kmProcesses, kmRequestID = 0;
int *pProcesses, pRequestID = 0, *pReserved;

int K,M,P,D,*z;
bool *H;

void cprintf(const char* format,...){
    char frmt[128] = "";

    sprintf(frmt,"%d : ",pid);
    {
        char tmp[10];
        sprintf(tmp,"%d : ",lclock);
        strcat(frmt,tmp);
    }
    va_list args;
    va_start(args,format);

    strcat(frmt,format);
    strcat(frmt,"\n");

    vfprintf(stdout,frmt,args);
    fflush(stdout);
    va_end(args);
}

void dream(int sec){
    struct timespec tim, tim2;
    tim.tv_sec = sec;
    tim.tv_nsec = rand()*rand()%1000000000L;
    nanosleep(&tim,&tim2);
}

void MY_Send(int *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm){

    cprintf(": Sending to %d",dest);
    int msg[2];
    msg[0] = buf[0];
    struct sembuf s;
    s.sem_num = 0;
    s.sem_op = -1;
    semop(clockSem, &s, 1);
    lclock++;
    s.sem_op = 1;
    semop(clockSem, &s, 1);
    msg[1] = lclock;

    dream(0);

    MPI_Send(msg,2,MPI_INT,dest,tag,comm);

    cprintf(": Sent %d to %d tagged %d",buf[0],dest,tag);
}

bool MY_Recv(int *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status){//returns true if receiver is older than sender

    cprintf(": Receiving from %d",source);

    struct sembuf s;
    s.sem_num = 0;
    s.sem_op = -1;
    dream(0);

    int msg[2];

    int clock;

    MPI_Recv(msg,2,datatype,source,tag,comm,status);
    
    buf[0] = msg[0];
    clock = msg[1];

    bool older = lclock>clock;
    if (lclock == clock && pid > status->MPI_SOURCE)
        older = true;
    semop(clockSem, &s, 1);
    lclock = (older?lclock:clock)+1;
    s.sem_op = 1;
    semop(clockSem, &s, 1);

    cprintf(": Recv %d from %d tagged %d",buf[0],status->MPI_SOURCE,status->MPI_TAG);

    return older;
}

void initVars(int argc, char** argv){
    if(argc!=size+5){
        fprintf(stderr,"Number of parameters isn't proper (1 - K, 2 - M, 3 - P, ... - z)!\n");
        MPI_Abort(MPI_COMM_WORLD,1);
    }
    
    clockSem = semget(1234, 1, IPC_CREAT);
    semctl(clockSem, 0, SETVAL, 1);
    sscanf(argv[1],"%d",&K);
    sscanf(argv[2],"%d",&M);
    sscanf(argv[3],"%d",&P);
    sscanf(argv[4],"%d",&D);

    if(!((M<K) & (K<P) & (P<D) & (size<=D))){
        fprintf(stderr,"M(%d)<K(%d)<P(%d)<D(%d)<=np(%d)\n",M,K,P,D,size);
        MPI_Abort(MPI_COMM_WORLD,1);
    }

    z = (int*)malloc(size*sizeof(int));
    H = (bool*)malloc(D*sizeof(bool));
    kmProcesses = (int*) malloc(size * sizeof(int));
    pProcesses = (int*) malloc(size * sizeof(int));
    pReserved = (int*) malloc(size * sizeof(int));
    {
        int i=0;
        for(;i<size ; i++){
            sscanf(argv[i+5],"%d",&(z[i]));
            if(z[i]>P){
                fprintf(stderr,"z[%d]<P\n",i);
                MPI_Abort(MPI_COMM_WORLD,1);
            }
            kmProcesses[i] = -1;
            pProcesses[i] = -1;
            if (i != pid)
                pReserved[i] = z[i];
            else 
                pReserved[i] = 0;
        }
        for(i=0;i<D ; i++){
            H[i] = true;
        }
    }

    if(pid == 0){
        cprintf("K=%d",K);
        cprintf("M=%d",M);
        cprintf("P=%d",P);
        cprintf("D=%d",D);
        int i=0;
        for(;i<size;i++){
            cprintf("z[%d]=%d",i,z[i]);
        }
        i=0;
        for(;i<D;i++){
            cprintf("H[%d]=%d",i,H[i]);
        }
    }
}

void *answerer(void *arg){
    int buf,countD=0, countKM = 0;
    bool older;
    MPI_Status status;
    while(!stop){
        older = MY_Recv(&buf,1,MPI_INT,MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
        
        switch(status.MPI_TAG){
            case DREQ:
                if((di==buf && older) || !H[buf]){
                    buf = -1;
                } else if(di==buf && !older){
                    pstat = 3;
                    countD = 0;
                }

                MY_Send(&buf,1,MPI_INT,status.MPI_SOURCE,DACK,MPI_COMM_WORLD);        
                break;
            case DACK:
                if(buf == -1){
                    pstat = 3;
                    countD = 0;
                } else if (buf == di && pstat == 1){
                    countD++;
                    if(countD==size-1){
                        pstat = 2;
                        countD = 0;
                    }
                }
                break;
            case KMREQ:
                older = (buf < kmRequestID || (buf == kmRequestID && pid > status.MPI_SOURCE));
                if (pstat == 0 || (pstat == 4 && older)) {
                    MY_Send(&buf, 1, MPI_INT, status.MPI_SOURCE, KMACK, MPI_COMM_WORLD);
                } else {
                    kmProcesses[status.MPI_SOURCE] = buf;
                    buf = -1;
                }
                break;
            case KMACK:
                if (buf == kmRequestID) {
                    if (++countKM >= size - M)
                        pstat = 5;
                }
                break;
            case PREQ:
                older = (buf < pRequestID || (buf == kmRequestID && pid > status.MPI_SOURCE));
                if (pstat == 0 || pstat == 4 || pstat == 5 || (pstat == 6 && older)) {
                    pReserved[status.MPI_SOURCE] = z[status.MPI_SOURCE];
                    MY_Send(&buf, 1, MPI_INT, status.MPI_SOURCE, PACK, MPI_COMM_WORLD);
                } else {
                    buf = -1;
                    pProcesses[status.MPI_SOURCE] = buf;
                }
                break;
            case PACK:
                if (buf == pRequestID) {
                    pReserved[status.MPI_SOURCE] = 0;
                    int sum = 0;
                    for (buf = 0; buf < size; buf++)
                        sum += pReserved[buf];
                    if (P - sum >= z[pid])
                        pstat = 7;
                }
                break;
            default:
                cprintf("Unknown message: %d, tagged: %d",buf,status.MPI_TAG);
        }
    }
    pthread_exit(NULL);
}

void lockP() {
    int i;
    pstat = 6;
    for (i = 0; i < size; i++) {
        if (i == pid)
            continue;
        MY_Send(&pRequestID, 1, MPI_INT, i, PREQ, MPI_COMM_WORLD);
    }
    while (pstat != 7);
}

void lockKP() {
    int i;
    pstat = 4;
    for (i = 0; i < size; i++) {
        if (i == pid)
            continue;
        MY_Send(&kmRequestID, 1, MPI_INT, i, KMREQ, MPI_COMM_WORLD);
    }
    while (pstat != 5);
    
}

void lockD(){
    di = pid * D/size;
    
    do{
        pstat = 1;

        while(!H[di]) di=(di+1)%D;    

        cprintf(": Tries %d",di);

        int k;
        for(k=0 ; k<size ; k++){
            if(k==pid) continue;
            MY_Send(&di,1,MPI_INT,k,DREQ,MPI_COMM_WORLD);
        }
        while(pstat==1)dream(0);

        di = pstat==3?(di+1)%D:di;
    } while(pstat==3);

    H[di] = false;
    if(di!=dj) H[dj] = true;
    dj = di;
}

void putEverythingBack() {
    int i;
    pstat = 0;
    kmRequestID++;
    pRequestID++;
    for (i = 0; i < size; i++) {
        if (i == pid)
            continue;
        if (kmProcesses[i] != -1) {
            MY_Send(&kmProcesses[i], 1, MPI_INT, i, KMACK, MPI_COMM_WORLD);
            kmProcesses[i] = -1;
        }
    }
    for (i = 0; i < size; i++) {
        if (i == pid)
            continue;
        if (pProcesses[i] != -1) {
            MY_Send(&pProcesses[i], 1, MPI_INT, i, PACK, MPI_COMM_WORLD);
            pProcesses[i] = -1;
        }
    }
    for (i = 0; i < size; i++)
        if (i != pid)
            pReserved[i] = z[i];
}

int main(int argc, char** argv){
    MPI_Init(NULL,NULL);

    MPI_Comm_size(MPI_COMM_WORLD,&size);
    MPI_Comm_rank(MPI_COMM_WORLD,&pid);

    initVars(argc,argv);    

    srand(time(NULL));
    
    MPI_Barrier(MPI_COMM_WORLD);

    pthread_t thread;
    if((pthread_create(&thread,NULL,answerer,NULL))){
        fprintf(stderr,"Error on pthread_create\n");
        MPI_Abort(MPI_COMM_WORLD,1);
    }
    int i;
    for(i=0 ; i<LIMIT ; i++){
        //----------KASPRZAK & FOG------------
        //cprintf("Wants to take Kasprzak and fog machine");
        lockKP();
        cprintf("Kasprzak and fog machine taken");
        dream(0);
        //----------Przescieradla---------
        cprintf("Wants to take %d sheets", z[pid]);
        lockP();
        cprintf("Sheet taken");
        //----------HOUSE-----------------
        cprintf("Wants to enter house %d. time",i+1);
        lockD();
        cprintf("Entered house %d",di);
        dream(rand()%6);
        cprintf("Left house %d",di);
        dream(rand()%3);
            putEverythingBack();
    }

    cprintf("%d. process finished.",pid);
    MPI_Barrier(MPI_COMM_WORLD);

    stop = true;
    free(H);
    free(z);
    MPI_Finalize();
    pthread_join(thread,NULL);    
}

