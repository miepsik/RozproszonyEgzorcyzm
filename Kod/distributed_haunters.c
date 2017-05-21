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
int* lclock = NULL;

typedef struct lclocked_int_s {
	int* lclock;
	int value;
} lclocked_int;

//MPI_Datatype MY_LCLOCKED_INT;

void cprintf(const char* format,...){
	if(lclock == NULL){
		fprintf(stderr,"Clock has not been initialized");
		exit(-1);
	}

	char frmt[128] = "";
	{
		sprintf(frmt,"%d : ",pid);
		int i=0;
		for(;i<size;i++){
			char tmp[10];
			sprintf(tmp,"%c%d",i>0?',':'[',lclock[i]);
			strcat(frmt,tmp);
		}
		strcat(frmt,"] :\t");
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
	tim.tv_sec = 1;
	tim.tv_nsec = rand()%1000;
	nanosleep(&tim,&tim2);
}

void MY_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm){

	dream();

	lclock[pid]++;
	lclocked_int pckt;
	pckt.lclock = (int *)malloc(size*sizeof(int));
	memcpy(pckt.lclock,lclock,sizeof(int)*size);

	pckt.value = *((int *) buf);
	MPI_Send(&(pckt.value),count,datatype,dest,tag,comm);
	MPI_Send(pckt.lclock,size,MPI_INT,dest,CLOCK,comm);
	free(pckt.lclock);
}

void MY_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status){

	dream();

	lclocked_int pckt;	
	pckt.lclock = (int *)malloc(size*sizeof(int));
	MPI_Recv(&(pckt.value),count,datatype,source,tag,comm,status);
	MPI_Recv(pckt.lclock,size,MPI_INT,source,CLOCK,comm,status);

	*((int*)buf) = pckt.value;

	{
		int i=0;
		for(;i<size ; i++){
			if(lclock[i]<pckt.lclock[i])
				lclock[i] = pckt.lclock[i];
		}
		lclock[pid]++;
	}

	free(pckt.lclock);
}
/*
void initStructs(){
	//MY_LCLOCKED_INT
	const int n_items = 2;
	int blocklengths[2] = {size,1};
	MPI_Datatype types[2] = {MPI_INT,MPI_INT};
	MPI_Aint offsets[2];
	offsets[0] = offsetof(lclocked_int,lclock);
	offsets[1] = offsetof(lclocked_int,value);

	MPI_Type_create_struct(n_items,blocklengths,offsets,types,&MY_LCLOCKED_INT);
	MPI_Type_commit(&MY_LCLOCKED_INT);
}
*/
int main(int argc, char** argv){
	srand(time(NULL));

	MPI_Init(NULL,NULL);

	MPI_Comm_size(MPI_COMM_WORLD,&size);

	if(argc!=size+4){
		fprintf(stderr,"Number of parameters isn't proper (1 - K, 2 - M, 3 - P, ... - z)!\n");
		exit(-1);
	}
	
	lclock = (int *)malloc(size*sizeof(int));
	{
		int i = 0;	
		for(; i<size ; i++)
			lclock[i]=0;
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
	


	//initStructs();

	

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

//	MPI_Type_free(&MY_LCLOCKED_INT);
	MPI_Finalize();
	free(lclock);
}
