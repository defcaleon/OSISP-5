#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <fcntl.h>
#include <wait.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#define SIGUSR_LIMIT 101
#define PROC_COUNT 9

const int childsCount[PROC_COUNT] = {1, 1, 3, 1, 1, 1, 0, 0, 0};
const int signalType[PROC_COUNT] = {0, SIGUSR1, SIGUSR1, SIGUSR2, SIGUSR1, 0, 0, SIGUSR2, SIGUSR1};
const int sendTo[PROC_COUNT][3] = {{1}, {2}, {5,4, 3}, {7}, {6}, {8}, {}, {4,5,6}, {1}};

int procId = 0;
pid_t* pids;
int* readyFlags;
int sendReceiveAmount[2][2] = { {0, 0}, {0, 0} }; // s,r SU1, s,r SU2

int setupProcess(int);
void waitForChild();
void killWithWait();
void setSignalHandler(void (*handler)(int), int signalNumber, int flags);
void signalHandler(int signalNumber);

int main()
{
    pids = (pid_t*)mmap(pids, (PROC_COUNT)*sizeof(pid_t), PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);  
    readyFlags = (int*)mmap(readyFlags, (PROC_COUNT)*sizeof(int), PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    setupProcess(0);
    setSignalHandler(&killWithWait, SIGTERM, 0);

    if (procId == 0)
    {
        waitForChild();
        return 0;
    }

    pids[procId] = getpid();

    int i;
    //printf("procId: %d\tpid: %d\tppid: %d; pgid: %d\n", procId, getpid(), getppid(), getpgid(0));
    fflush(stdout);


    if (procId == 1)
    {
        do{
            for (i = 1; (i <= PROC_COUNT) && (pids[i] != 0); ++i) {

                if (pids[i] == -1) {
                    printf("Error with processes fork\n");
                    exit(1);
                }
            }
        } while (i < PROC_COUNT);


        printf("All handlers were set\n");
        pids[0] = 1;

        setSignalHandler(&signalHandler, 0, 0);

        do{
            for (i = 1; (i < PROC_COUNT)  && (readyFlags[i] != 0); ++i) {

                if (pids[i] == -1) {
                    printf("Error with handlers initialization\n");
                    exit(1);
                }
            }
        } while (i < PROC_COUNT);

        signalHandler(0);
    }
    else
    {
        while (pids[0] == 0);

        setSignalHandler(&signalHandler, 0, 0);
    }

    while (1)
    {
        pause();
    }
    return 0;
}

void waitForChild()
{
    int i = childsCount[procId];
    while (i > 0)
    {
        int status = 0;
        wait(&status);
        i--;
    }
}

void killWithWait() {
    for (int i = 0; i < childsCount[procId]; ++i) {
        kill(pids[sendTo[procId][i]], SIGTERM);
    }

    waitForChild();

    if (procId != 0){
        printf("proc %d with pid %d and ppid %d was terminated after sending %d SIGUSR1 и %d SIGUSR2\n", procId, 
                getpid(), getppid(), sendReceiveAmount[0][1], sendReceiveAmount[1][1]);
        fflush(stdout);
    }

    exit(0);
}  

void setSignalHandler(void (*handler)(int), int signalNumber, int flags)
{
    sigset_t sigSet;
    sigemptyset(&sigSet);
    sigaddset(&sigSet, SIGUSR1);
    sigaddset(&sigSet, SIGUSR2);

    struct sigaction sigAction;
    sigAction.sa_mask = sigSet;
    sigAction.sa_flags = flags;
    sigAction.sa_handler = handler;

    if (signalNumber != 0)
    {
        sigaction(signalNumber, &sigAction, NULL);
        return;
    }
    else
    {
        for (int i = 0; i < PROC_COUNT; i++)
        {
            int destination[2];
            destination[0] = sendTo[i][0];
            destination[1] = sendTo[i][1];
            for (int j = 0; j < 2; j++){
                if (destination[j] == procId)
                {
                    if (signalType[i] != 0)
                    {
                        sigaction(signalType[i], &sigAction, 0);
                    }
                }
            }
        }
    }
    readyFlags[procId] = 1;
}

void signalHandler(int signalNumber)
{
    if (signalNumber == SIGUSR1) signalNumber = 0;
    else if (signalNumber == SIGUSR2) signalNumber = 1;
    else signalNumber = -1;

    if (signalNumber != -1)
    {
        sendReceiveAmount[signalNumber][0]++;
        struct timespec tp;
        clock_gettime(0, &tp);
        
        printf("%d <- %s%d\n",  procId,
               "SIGUSR", signalNumber + 1);
        fflush(stdout);

        if (procId == 1)
        {
            if (sendReceiveAmount[0][0] + sendReceiveAmount[1][0] >= SIGUSR_LIMIT)
            {
                killWithWait();
            }
        }
    }

    if (signalType[procId] > 0){
        int destination[2];
        destination[0] = sendTo[procId][0];
        destination[1] = sendTo[procId][1];
        for (int i = 0; i < 2; i++){
            if (destination[i] != 0){
                if (signalType[procId] == SIGUSR1){
                    signalNumber = 0;
                }
                else{
                    signalNumber= 1;
                }
                sendReceiveAmount[signalNumber][1]++;

                struct timespec tp;
                clock_gettime(0, &tp);
                printf("%d -> %s%d %d\n", procId,
                        "SIGUSR", signalNumber+1, destination[i]);
                fflush(stdout);

                kill(pids[destination[i]], signalType[procId]);
            }
        }
    }

}

int setupProcess(int id)
{
    for (int i = 0; i < childsCount[id]; i++)
    {
        pid_t pid = fork();
        int childId = sendTo[id][i];
        if (pid == 0)
        {
            procId = childId;
            if (childsCount[childId] != 0)
            {
                setupProcess(childId);
            }
            break;
        }
    }
}