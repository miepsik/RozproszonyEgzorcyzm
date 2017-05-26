#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#include <pthread.h>

#define LIMIT 2

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

void MY_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm){

	dream(0);

	lclock++;

	MPI_Send(buf,count,datatype,dest,tag,comm);

	//cprintf("Sent %d to %d tagged %d",*((int *) buf),dest,tag);

	MPI_Send(&lclock,1,MPI_INT,dest,CLOCK,comm);

	//cprintf("Sent clock");
}

bool MY_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status){//returns true if receiver is older than sender

	dream(0);

	int clock;

	MPI_Recv(buf,count,datatype,source,tag,comm,status);

	//cprintf("Recv %d from %d tagged %d (%d,%d)",*((int *)buf),source,tag,status->MPI_SOURCE,status->MPI_TAG);

	MPI_Recv(&clock,1,MPI_INT,status->MPI_SOURCE,CLOCK,comm,NULL);

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

	if(!(K<M<P<D)){
		fprintf(stderr,"K<M<P<D\n");
		MPI_Abort(MPI_COMM_WORLD,1);
	}

	z = (int*)malloc(size*sizeof(int));
	H = (bool*)malloc(D*sizeof(bool));
	{
		int i=0;
		int tmp = 0;
		for(;i<size ; i++){
			sscanf(argv[i+5],"%d",&(z[i]));
			tmp += z[i];
		}
		for(i=0;i<D ; i++){
			H[i] = true;
		}
		if(tmp!=P){
			fprintf(stderr,"sum(z)!=P (%d!=%d)\n",tmp,P);
			MPI_Abort(MPI_COMM_WORLD,1);
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
		for(;i<size;i++){
			cprintf("H[%d]=%d",i,H[i]);
		}
	}
}

void *answerer(void *arg){
	int buf,countD=0;
	bool older;
	MPI_Status status;
	while(!stop){
		//cprintf("Loop %d",stop);

		older = MY_Recv(&buf,1,MPI_INT,MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
		
		//cprintf("buf %d ; src %d ; tag %d",buf,status.MPI_SOURCE,status.MPI_TAG);

		switch(status.MPI_TAG){
			case DREQ:
				//Jeśli proces otrzymujący wiadomość nie chce straszyć w tym domu, lub jest młodszy wysyła zgodę. W przeciwnym wypadku wysyła zabronienie.
				if(di==buf && older)
					buf = -1;
				MY_Send(&buf,1,MPI_INT,status.MPI_SOURCE,DACK,MPI_COMM_WORLD);		
				break;
			case DACK:
				if(buf == -1){
					pstat = 3;
					countD = 0;
				} else if (buf == di){
					countD = (countD==size-1)?1:countD+1;
					if(countD==size-1){
						//Jeśli proces otrzyma od wszystkich zgodę może straszyć w domu, ustawia $H[i] = false$, oraz $H[j] = true$, gdzie $j$ to identyfikator domu w którym straszył sotatnio. 
						pstat = 2;
						countD = 0;
						H[di] = false;
						H[dj] = true;
						dj = di;
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
	//Proces zaczyna rezerwację domów od domu o numerze $ i = pid * \frac{D}{n}$.
	pstat = 1;
	di = pid * D/size;
	
	do{
		//cprintf("pstat %d di %d",pstat,di);
		//Jeśli $H[i] == false$ to proces stara się o rezerwację domu o numerze $(++i) \% D$.
		while(!H[di]) di=(di+1)%D;	

		//Proces wysyła do wszystkich pozostałych procesów informację o domu w którym chce straszyć.
	    	int k;
		for(k=0 ; k<size ; k++){
			if(k==pid) continue;
			MY_Send(&di,1,MPI_INT,k,DREQ,MPI_COMM_WORLD);
		}
		while(pstat==1)dream(1);

		//Jeśli otrzyma chociaż jedno zabronienie stara się o rezerwację domu o numerze $(++i) \% D$
		di = pstat==3?(di+1)%D:di;
	} while(pstat==3);
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
		cprintf("Wants to enter house");
		lockD();
		cprintf("Entered house %d",di);
		dream(rand()%10);
		cprintf("Wants to leave house");
		//unlockD();
		cprintf("Left house");
		dream(rand()%5);
	}

	stop = true;
	free(H);
	free(z);
	pthread_join(thread,NULL);	
	MPI_Finalize();
}
