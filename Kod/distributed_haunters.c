#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#include <pthread.h>

#define LIMIT 20

//tags:
#define CLOCK 111 //Lamport's clock
#define DREQ 222 //House request
#define DACK 333 //House acknowledge

bool stop = false;

int di,dj;
int pstat = 0;
/*
status grupy egzorcystow
0	nic nie chca
1	chca dom
2	otrzymali dom
3	nie dostana danego domu
*/

int size,pid;
int lclock = 0;

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

	//cprintf("Sending to %d",dest);
    int msg[2];
    msg[0] = buf[0];
    msg[1] = ++lclock;

	dream(0);

	MPI_Send(msg,2,MPI_INT,dest,tag,comm);

	//cprintf("Sent %d to %d tagged %d",*((int *) buf),dest,tag);

	//MPI_Send(&lclock,1,MPI_INT,dest,CLOCK,comm);

	//cprintf("Sent clock");
}

bool MY_Recv(int *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status){//returns true if receiver is older than sender

	//cprintf("Receiving from %d",source);

	dream(0);

    int msg[2];

	int clock;

	MPI_Recv(msg,2,datatype,source,tag,comm,status);
    
    buf[0] = msg[0];
    clock = msg[1];

	//cprintf("Recv %d from %d tagged %d",*((int *)buf),status->MPI_SOURCE,status->MPI_TAG);

	//MPI_Recv(&clock,1,MPI_INT,status->MPI_SOURCE,CLOCK,comm,NULL);

	//cprintf("Recv clock");

	bool older = lclock>clock;
	lclock = (older?lclock:clock)+1;

	return older;
}

void initVars(int argc, char** argv){
	if(argc!=size+5){
		fprintf(stderr,"Number of parameters isn't proper (1 - K, 2 - M, 3 - P, ... - z)!\n");
		MPI_Abort(MPI_COMM_WORLD,1);
	}
	
	
	sscanf(argv[1],"%d",&K);
	sscanf(argv[2],"%d",&M);
	sscanf(argv[3],"%d",&P);
	sscanf(argv[4],"%d",&D);

	if(!((K<M) & (M<P) & (P<D) & (size<D))){
		fprintf(stderr,"K<M<P<D\n");
		MPI_Abort(MPI_COMM_WORLD,1);
	}

	z = (int*)malloc(size*sizeof(int));
	H = (bool*)malloc(D*sizeof(bool));
	{
		int i=0;
		for(;i<size ; i++){
			sscanf(argv[i+5],"%d",&(z[i]));
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
	int buf,countD=0;
	bool older;
	MPI_Status status;
	while(!stop){
		//cprintf("countD=%d, pstat=%d,di=%d, dj=%d, H[i]=%d, H[j]=%d",countD,pstat,di,dj, H[di],H[dj]);		
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
			default:
				cprintf("Unknown message: %d, tagged: %d",buf,status.MPI_TAG);
		}
	}
	pthread_exit(NULL);
}

void lockD(){
	di = pid * D/size;
	
	do{
		pstat = 1;

		while(!H[di]) di=(di+1)%D;	
		//cprintf("Tries %d",di);

	    	int k;
		for(k=0 ; k<size ; k++){
			if(k==pid) continue;
			MY_Send(&di,1,MPI_INT,k,DREQ,MPI_COMM_WORLD);
		}
		while(pstat==1)dream(0);

		di = pstat==3?(di+1)%D:di;
	} while(pstat==3);

	H[di] = false;
	H[dj] = true;
	dj = di;
}

void hprint(){
	int i;
	char tmp[30] = "";
	for(i=0 ; i<D ; i++){
		sprintf(tmp,"%s%c%d",tmp,i>0?',':'(',H[i]);
	}
	strcat(tmp,")");
	cprintf("%s",tmp);
}

int main(int argc, char** argv){
	MPI_Init(NULL,NULL);

	MPI_Comm_size(MPI_COMM_WORLD,&size);
	MPI_Comm_rank(MPI_COMM_WORLD,&pid);

	initVars(argc,argv);	

	srand(time(NULL));

	MPI_Barrier(MPI_COMM_WORLD);

	//-----------TEST-----------

	pthread_t thread;
	if((pthread_create(&thread,NULL,answerer,NULL))){
		fprintf(stderr,"Error on pthread_create\n");
		MPI_Abort(MPI_COMM_WORLD,1);
	}
	int i;
	for(i=0 ; i<LIMIT ; i++){
		cprintf("Wants to enter house %d time",i);
		hprint();
		lockD();
		hprint();
		cprintf("Entered house %d",di);
		dream(rand()%2);
		//cprintf("Wants to leave house");
		//unlockD();
		cprintf("Left house %d",di);
		hprint();
		dream(rand()%2);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	stop = true;
	free(H);
	free(z);
	MPI_Finalize();
	pthread_join(thread,NULL);	
}
