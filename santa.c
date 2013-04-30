#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>

//@todo: kill všech kromě

//označení parametrů
enum PARAMCODES
{
	C, E, H, S
};

//sdílená data
typedef struct
{
	FILE* f;
	int actionCounter, onVacation, canHandle, waiting, pidIndex;
	pid_t pids[1];

} Tshared;

#define SEMCOUNT 7
// indexování semaforů
enum {IO, SHARED, FINISH, GOT, ASK, ACTION, VACATION};
// jména semaforů
const char* semNames[] =
{"/xvokra00_io", "/xvokra00_shared", "/xvokra00_finish", "/xvokra00_got", "/xvokra00_ask", "/xvokra00_action", "/xvokra00_vacation"};

/**
 * Čtení parametrů
 * @param paramCount - počet parametrů
 * @param paramStrings - pole argumentů
 * @param params - pole pro uložení zpracovaných parametrů
 * @return -1 při chybě, 0 ok
*/
int readParams(int paramCount, char* paramStrings[], int* params)
{
	char* endptr;
	int i;

	if(paramCount != 4) return -1;

	for(i = 0; i < paramCount; ++i)
	{
		params[i] = strtol(paramStrings[i], &endptr, 10);
		if(*endptr != '\0' || params[i] < 0) return -1;
	}

	if(params[C] == 0 || params[E] == 0) return -1;

	return 0;
}

/**
 * Vytvoření semaforů
 * @param semaphores - odkaz na pole semaforů pro uložení
 * @param elfs - počet elfů - inicializace semaforu pro počet puštěných k santovi
 * @return int - počet úspěšně vytvořených semaforů pro jejich odstranění
*/
int createSemaphores(sem_t* semaphores[])
{
	for(int i = 0; i < SEMCOUNT; ++i)
	{
		// i < 2 zamčené jsou všechny kromě IO a SHARED
		semaphores[i] = sem_open(semNames[i], O_CREAT|O_EXCL, 0666, i < 2);
		if(semaphores[i] == SEM_FAILED) return i;
	}

	return SEMCOUNT;
}

/**
 * Zavření semaforů
 * @param semaphores - ukazatel na pole semaforů
*/
void closeSemaphores(sem_t* semaphores[], int semsOpen)
{
	for(int i = 0; i < semsOpen; ++i)
	{
		sem_close(semaphores[i]);
	}
}

/**
 * Zničení semaforů
*/
void unlinkSemaphores(int semsOpen)
{
	for(int i = 0; i < semsOpen; ++i)
	{
		sem_unlink(semNames[i]);
	}
}

/**
 * Zpracování signálu pro ukončení
*/
void suicide()
{
	exit(3);
}

/**
 * Zamknutí semaforu s ošetřením chyb
 * @param sem - semafor
*/
void semWait(sem_t* sem)
{
	int res = sem_wait(sem);
	if(res == -1)
	{
		perror(NULL);
		suicide();
	}
}

/**
 * Odemknutí semaforu s ošetřením chyb
 * @param sem - semafor
*/
void semPost(sem_t* sem)
{
	int res = sem_post(sem);
	if(res == -1)
	{
		perror(NULL);
		suicide();
	}
}

/**
 * Vytvoření a inicializace sdílené paměti
 * @param shmid - ukazatel pro uložení čísla, které se používá při ničení sdílené paměti
 * @param elfs - počet elfů pro alokaci struktury správné velikosti
 * @return ukazatel na sdílenou pamět, při chybě NULL
*/
Tshared* createSharedMemory(int *shmid, int elfs)
{
	Tshared* shared;

	*shmid = shmget(IPC_PRIVATE, sizeof(Tshared)+sizeof(int)*elfs, IPC_CREAT|0666);
	if(*shmid < 0)
	{
		return NULL;
	}

	shared = (Tshared*) shmat(*shmid, NULL, 0);
	if(shared == (void*) -1)
	{
		shmctl(*shmid, IPC_RMID, NULL);
		return NULL;
	}

	shared->pidIndex = 0;
	shared->waiting = 0;
	shared->onVacation = 0;
	shared->canHandle = elfs > 3 ? 3 : 1;

	return shared;
}

/**
 * Tisk výstupů do souboru
 * @param io - ukazatel na semafor pro přístup k souboru
 * @param shared - ukazatel na sdílenou paměť
 * @param fmt - formát tisknutého řetězce
 * @param elfID - identifikátor elfa
*/
void printStatus(sem_t* io, Tshared* shared, const char* fmt, int elfID)
{
	semWait(io);
	++shared->actionCounter;
	if(elfID == 0)
	{
		fprintf(shared->f, fmt, shared->actionCounter);
	}
	else
	{
		fprintf(shared->f, fmt, shared->actionCounter, elfID);
	}
	fflush(shared->f);
	semPost(io);
}

/**
 * Akce elfů
 * @param params - pole zpracovaných parametrů
 * @param semaphores - ukazatel na pole semaforů
 * @param shared - ukazatel na sdílenou paměť
 * @param elfID - identifikátor elfa
*/
int elf(int* params, sem_t* semaphores[], Tshared* shared, int elfID)
{
	printStatus(semaphores[IO], shared, "%d: elf: %d: started\n", elfID);
	int r = (int) getpid();

	//simulovaná práce
	for(int i = 0; i < params[C]; ++i)
	{
		srand(r);
		r = rand();
		usleep(r%(params[H]+1)*1000);
		printStatus(semaphores[IO], shared, "%d: elf: %d: needed help\n", elfID);
		semWait(semaphores[ASK]);
		printStatus(semaphores[IO], shared, "%d: elf: %d: asked for help\n", elfID);
		semPost(semaphores[ACTION]);

		semWait(semaphores[GOT]);
		printStatus(semaphores[IO], shared, "%d: elf: %d: got help\n", elfID);

		semWait(semaphores[SHARED]);
		--shared->waiting;
		if(i == params[C]-1)
		{
			++shared->onVacation;
			printStatus(semaphores[IO], shared, "%d: elf: %d: got a vacation\n", elfID);
			shared->canHandle = (params[E] - shared->onVacation) > 3 ? 3 : 1;
		}

		//uvolnění santa pro pomoc
		if(shared->waiting == 0)
		{
			for(int j = 0; j < shared->canHandle; ++j)
			{
				semPost(semaphores[ASK]);
			}
		}
		semPost(semaphores[SHARED]);
	}

	//santa může skončit
	if(shared->onVacation == params[E])
	{
		semPost(semaphores[VACATION]);
	}

	semWait(semaphores[FINISH]);
	semPost(semaphores[FINISH]);
	printStatus(semaphores[IO], shared, "%d: elf: %d: finished\n", elfID);

	shmdt(shared);
	closeSemaphores(semaphores, SEMCOUNT);

	return 0;
}

/**
 * Akce santy
 * @param params - pole zpracovaných parametrů
 * @param semaphores - ukazatel na pole semaforů
 * @param shared - ukazatel na sdílenou paměť
*/
int santa(int* params, sem_t* semaphores[], Tshared* shared)
{
	int waiting = 0;
	char fmt[100];
	int r = (int) getpid();
	int canAsk = params[E] > 3 ? 3 : 1;
	for(int i = 0; i < canAsk; ++i)
	{
		semPost(semaphores[ASK]);
	}

	printStatus(semaphores[IO], shared, "%d: santa: started\n", 0);

	//obsluha elfů
	for(int i = 0; i < params[C]*params[E]; ++i)
	{
		semWait(semaphores[ACTION]);
		semWait(semaphores[SHARED]);
		++shared->waiting;
		semPost(semaphores[SHARED]);

		//může pomoci
		if(shared->waiting == shared->canHandle)
		{
			waiting = shared->waiting;
			sprintf(fmt, "%%d: santa: checked state: %d: %d\n", params[E] - shared->onVacation, shared->waiting);
			printStatus(semaphores[IO], shared, fmt, 0);
			printStatus(semaphores[IO], shared, "%d: santa: can help\n", 0);
			srand(r);
			r = rand();
			usleep(r%(params[S]+1)*1000);

			//dostali pomoc
			for(int j = 0; j < waiting; ++j)
			{
				semPost(semaphores[GOT]);
			}
		}
	}

	//všichni elfové jsou na dovolené
	semWait(semaphores[VACATION]);
	sprintf(fmt, "%%d: santa: checked state: %d: %d\n", params[E] - shared->onVacation, shared->waiting);
	printStatus(semaphores[IO], shared, fmt, 0);

	//elfové se mohou ukončit
	semPost(semaphores[FINISH]);

	printStatus(semaphores[IO], shared, "%d: santa: finished\n", 0);

	shmdt(shared);
	closeSemaphores(semaphores, SEMCOUNT);

	return 0;
}

/**
 * Zabití všech vytvořených procesů v případě chyby
 * @param shared - ukazatel na sdílenou paměť
*/
void killEmAll(Tshared* shared)
{
	for(int i = 0; i < shared->pidIndex; ++i)
	{
		kill(shared->pids[i], SIGINT);
	}
}

/**
 * Zničení sdílené paměti a semaforů
 * @param semaphores - ukazatel na strukturu obsahující semafory
 * @param shared -u ukazatel na sdílenou paměť
 * @param shmid - číslo sdílené paměti
*/
void clean(sem_t* semaphores[], Tshared* shared, int shmid, int semsOpen)
{
	shmdt(shared);
	shmctl(shmid, IPC_RMID, NULL);
	closeSemaphores(semaphores, semsOpen);
	unlinkSemaphores(semsOpen);
}

int main(int argc, char* argv[])
{
	sem_t* semaphores[SEMCOUNT];
	Tshared* shared;
	int status, semsOpen;

	int params[4];
	int forkRes, shmid, paramsRes = readParams(argc-1, &argv[1], params);

	if(paramsRes != 0)
	{
		fprintf(stderr, "Chyba čtení parametrů\n");
		return 1;
	}

	semsOpen = createSemaphores(semaphores);
	if(semsOpen != SEMCOUNT)
	{
		closeSemaphores(semaphores, semsOpen);
		unlinkSemaphores(semsOpen);
		perror(NULL);
		return 1;
	}

	shared = createSharedMemory(&shmid, params[E]);

	if(shared == NULL)
	{
		closeSemaphores(semaphores, SEMCOUNT);
		unlinkSemaphores(SEMCOUNT);
		perror(NULL);
		return 2;
	}

	shared->f = fopen("santa.out", "w+");

	if(shared->f == NULL)
	{
		clean(semaphores, shared, shmid, SEMCOUNT);
		perror(NULL);
		return 2;
	}

	//vytvoření elfů
	for(int i = 0; i < params[E]; ++i)
	{
		forkRes = fork();
		if(forkRes == 0)
		{
			signal(SIGINT, suicide);
			semWait(semaphores[SHARED]);
			shared->pids[shared->pidIndex++] = getpid();
			semPost(semaphores[SHARED]);
			return elf(params, semaphores, shared, i+1);
		}
		else if(forkRes == -1)
		{
			perror(NULL);
			killEmAll(shared);
			clean(semaphores, shared, shmid, SEMCOUNT);
			return 2;
		}
	}

	//vytvoření santy
	forkRes = fork();
	if(forkRes == 0)
	{
		signal(SIGINT, suicide);
		semWait(semaphores[SHARED]);
		shared->pids[shared->pidIndex++] = getpid();
		semPost(semaphores[SHARED]);
		return santa(params, semaphores, shared);
	}
	else if(forkRes == -1)
	{
		perror(NULL);
		killEmAll(shared);
		clean(semaphores, shared, shmid, SEMCOUNT);
		return 2;
	}

	//čekání na ukončení všech vytvořených procesů
	for(int i = 0; i < params[E]+1; ++i)
	{

		wait(&status);
		if(status != 0)
		{
			killEmAll(shared);
			clean(semaphores, shared, shmid, SEMCOUNT);
			return 2;
		}
	}

	clean(semaphores, shared, shmid, SEMCOUNT);
	return 0;
}
