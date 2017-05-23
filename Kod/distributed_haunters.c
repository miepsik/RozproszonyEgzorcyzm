#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#define CLOCK 111

int size,pid;
int lclock = 0;

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

void dream(){
	struct timespec tim, tim2;
	tim.tv_sec = 0;
	tim.tv_nsec = rand()%1000000;
	nanosleep(&tim,&tim2);
}

void MY_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm){

	dream();

	lclock++;

	MPI_Send(buf,count,datatype,dest,tag,comm);
	MPI_Send(&lclock,1,MPI_INT,dest,CLOCK,comm);
}

bool MY_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status){//returns true if receiver is older than sender

	dream();

	int clock;

	MPI_Recv(buf,count,datatype,source,tag,comm,status);
	MPI_Recv(&clock,1,MPI_INT,source,CLOCK,comm,status);

	bool older = lclock>clock;
	lclock = (older?lclock:clock)+1;

	return older;
}

int main(int argc, char** argv){
	srand(time(NULL));

	MPI_Init(NULL,NULL);

	MPI_Comm_size(MPI_COMM_WORLD,&size);

	if(argc!=size+4){
		fprintf(stderr,"Number of parameters isn't proper (1 - K, 2 - M, 3 - P, ... - z)!\n");
		exit(-1);
	}
	
	int K,M,P,*z;
	bool *D;
	sscanf(argv[1],"%d",&K);
	sscanf(argv[2],"%d",&M);
	sscanf(argv[3],"%d",&P);
	z = (int*)malloc(size*sizeof(int));
	D = (bool*)malloc(size*sizeof(bool));
	{
		int i=0;
		for(;i<size ; i++){
			sscanf(argv[i+4],"%d",&(z[i]));
			D[i] = false;
		}
	}

	MPI_Comm_rank(MPI_COMM_WORLD,&pid);

	if(pid == 0){
		cprintf("K=%d",K);
		cprintf("M=%d",M);
		cprintf("P=%d",P);
		int i=0;
		for(;i<size;i++){
			cprintf("z[%d]=%d",i,z[i]);
		}
		i=0;
		for(;i<size;i++){
			cprintf("D[%d]=%d",i,D[i]);
		}
	}
	


	

	MPI_Barrier(MPI_COMM_WORLD);

	//-----------TEST-----------

	if(size!=2){
		fprintf(stderr,"The world is too small/big for %s!\n",argv[0]);
		MPI_Abort(MPI_COMM_WORLD,1);
	}

	int count = 0;
	int prank = (pid+1)%2;

	while(count<10){
		if(pid == count%2){
			count++;
			MY_Send(&count,1,MPI_INT,prank,0,MPI_COMM_WORLD);
			cprintf("%d to %d: %d",pid,prank,count);
		}else{
			MY_Recv(&count,1,MPI_INT,prank,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
			cprintf("%d from %d: %d",pid,prank,count);
		}
	}

	MPI_Finalize();
}
