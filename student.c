/*
 * student.c
 * This file contains the CPU scheduler for the simulation.  
 * original base code from http://www.cc.gatech.edu/~rama/CS2200
 * Last modified 10/20/2017 by Sherri Goings
 */



#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os-sim.h"
#include "student.h"

// Local helper function
static void schedule(unsigned int cpu_id);

/*
 * here's another way to do the thing I've used #define for in a couple of the past projects
 * which is to associate a word with each "state" of something, instead of having to 
 * remember what integer value goes with what actual state, e.g. using MOLO and OAHU
 * instead of 1 and 2 to designate an island in the boat project.
 * 
 * enum is useful C language construct to associate desriptive words with integer values
 * in this case the variable "alg" is created to be of the given enum type, which allows
 * statements like "if alg == FIFO { ...}", which is much better than "if alg == 1" where
 * you have to remember what algorithm is meant by "1"...
 * just including it here to introduce you to the idea if you haven't seen it before!
 */
typedef enum {
    FIFO = 0,
    RoundRobin,
    StaticPriority,
    MLF
} scheduler_alg;

typedef enum { false, true } bool;

scheduler_alg alg;

// MARK: - Function delcarations
void setState(int cpu_id, process_state_t state);
pcb_t* getProcessForCpuId(int cpu_id);
void print(char *text);
pcb_t* getLowestPriorityProcess(pcb_t* process);
bool hasEmptyCpus();
int getCpuIdForProcess(pcb_t* process);
void addToQueue(pcb_t* proc, int priority);
pcb_t* removeFromQueue(int priority);
pcb_t* getHighestMLF();


// declare other global vars
int time_slice = -1;
int cpu_count;
int preempting = 0;

pthread_mutex_t preempt_mutex;
pthread_mutex_t ml_mutex;
pthread_cond_t ml_empty;

//Multi-level ready queues

static pcb_t* mlhead1 = NULL;
static pcb_t* mltail1 = NULL;

static pcb_t* mlhead2 = NULL;
static pcb_t* mltail2 = NULL;

static pcb_t* mlhead3 = NULL;
static pcb_t* mltail3 = NULL;

static pcb_t* mlhead4 = NULL;
static pcb_t* mltail4 = NULL;

/*
 * main() parses command line arguments, initializes globals, and starts simulation
 */
int main(int argc, char *argv[])
{

    /* Parse command line args - must include num_cpus as first, rest optional
     * Default is to simulate using just FIFO on given num cpus, if 2nd arg given:
     * if -r, use round robin to schedule (must be 3rd arg of time_slice)
     * if -p, use static priority to schedule
     */
    if (argc == 2) {
        alg = FIFO;
        printf("running with basic FIFO\n");
    }
    else if (argc > 2 && strcmp(argv[2],"-r")==0 && argc > 3) {
        alg = RoundRobin;
        time_slice = atoi(argv[3]);
        printf("running with round robin, time slice = %d\n", time_slice);
    }
    else if (argc > 2 && strcmp(argv[2],"-p")==0) {
        alg = StaticPriority;
        printf("running with static priority\n");
    }
    else if (argc > 2 && strcmp(argv[2],"-m")==0 && argc > 3) {
        alg = MLF;
        time_slice = atoi(argv[3]);
        printf("running with multi-level feedback\n");
    }
    else {
        fprintf(stderr, "Usage: ./os-sim <# CPUs> [ -r <time slice> | -p ]\n"
                "    Default : FIFO Scheduler\n"
                "         -r : Round-Robin Scheduler (must also give time slice)\n"
                "         -p : Static Priority Scheduler\n\n");
        return -1;
    }
    fflush(stdout);

    /* atoi converts string to integer */
    cpu_count = atoi(argv[1]);

    /* Allocate the current[] array of cpus and its mutex */
    current = malloc(sizeof(pcb_t*) * cpu_count);
    int i;
    for (i=0; i<cpu_count; i++) {
        current[i] = NULL;
    }
    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);

    /* Initialize other necessary synch constructs */
    pthread_mutex_init(&ready_mutex, NULL);
    pthread_cond_init(&ready_empty, NULL);
    pthread_mutex_init(&preempt_mutex, NULL);
    pthread_mutex_init(&ml_mutex,NULL);
    pthread_cond_init(&ml_empty, NULL);

    /* Start the simulator in the library */
    printf("starting simulator\n");
    fflush(stdout);
    start_simulator(cpu_count);


    return 0;
}

/*
 * idle() is called by the simulator when the idle process is scheduled.
 * It blocks until a process is added to the ready queue, and then calls
 * schedule() to select the next process to run on the CPU.
 * 
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void idle(unsigned int cpu_id)
{
    pthread_mutex_lock(&ready_mutex);
    if(alg == MLF){
        pthread_mutex_lock(&ml_mutex);
        while(!mlhead1 && !mlhead2 && !mlhead3 && !mlhead4){
            pthread_cond_wait(&ml_empty, &ml_mutex);
        }
        pthread_mutex_unlock(&ml_mutex);
    }
    else{
        while (head == NULL) {
            pthread_cond_wait(&ready_empty, &ready_mutex);
        }
    }
    pthread_mutex_unlock(&ready_mutex);
    schedule(cpu_id);
}

/*
 * schedule() is your CPU scheduler. It currently implements basic FIFO scheduling -
 * 1. calls getReadyProcess to select and remove a runnable process from your ready queue 
 * 2. updates the current array to show this process (or NULL if there was none) as
 *    running on the given cpu 
 * 3. sets this process state to running (unless its the NULL process)
 * 4. calls context_switch to actually start the chosen process on the given cpu
 *    - note if proc==NULL the idle process will be run
 *    - note the final arg of -1 means there is no clock interrupt
 *	context_switch() is prototyped in os-sim.h. Look there for more information. 
 *  a basic getReadyProcess() is implemented below, look at the comments for info.
 *
 * TO-DO: handle scheduling with a time-slice when necessary
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
static void schedule(unsigned int cpu_id) {
    // selects and removes a runnable process from your ready queue
    pcb_t* proc = alg == MLF ? getHighestMLF(): getReadyProcess();
    //printf("proc name: %s\n", proc -> name);
    // updates array to show process (thread safe)
    pthread_mutex_lock(&current_mutex);
    current[cpu_id] = proc;
    pthread_mutex_unlock(&current_mutex);

    // updates process state
    if (proc!=NULL) {
        proc->state = PROCESS_RUNNING;
    }

    // switches context
    context_switch(cpu_id, proc, time_slice); 
}


/*
 * preempt() is the handler called by the simulator when a process is
 * preempted due to its timeslice expiring.
 * cpu_id is the index of this cpu in the "current" array of cpu's.
 *
 * This function should get the process currently running on the given cpu, 
 * change the process state to ready, place the process back in the
 * ready queue (for FIFO just use addReadyProcess), and finally call 
 * schedule() for this cpu to select a new runnable process.
 *
 * THIS FUNCTION MUST BE IMPLEMENTED FOR ROUND ROBIN OR PRIORITY SCHEDULING
 */
extern void preempt(unsigned int cpu_id) {
    if(alg == MLF){
        pcb_t* proc = getProcessForCpuId(cpu_id);
        setState(cpu_id, PROCESS_READY);
        addToQueue(proc, 2);
    }
    else{
        setState(cpu_id, PROCESS_READY);
        addReadyProcess(getProcessForCpuId(cpu_id));
    }
    schedule(cpu_id);
}


/*
 * yield() is called by the simulator when a process performs an I/O request 
 * note this is different than the concept of yield in user-level threads!
 * In this context, yield sets the state of the process to waiting (on I/O),
 * then calls schedule() to select a new process to run on this CPU.
 * args: int - id of CPU process wishing to yield is currently running on.
 * 
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void yield(unsigned int cpu_id) {
    // use lock to ensure thread-safe access to current process
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_WAITING;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}


/*
 * terminate() is called by the simulator when a process completes.
 * marks the process as terminated, then calls schedule() to select
 * a new process to run on this CPU.
 * args: int - id of CPU process wishing to terminate is currently running on.
 *
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void terminate(unsigned int cpu_id) {
    // use lock to ensure thread-safe access to current process
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_TERMINATED;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}

/*
 * wake_up() is called for a new process and when an I/O request completes.
 * The current implementation handles basic FIFO scheduling by simply
 * marking the process as READY, and calling addReadyProcess to put it in the
 * ready queue.  No locks are needed to set the process state as its not possible
 * for anyone else to also access it at the same time as wake_up
 *
 * TO-DO: If the scheduling algorithm is static priority, wake_up() may need
 * to preempt the CPU with the lowest priority process to allow it to
 * execute the process which just woke up.  However, if any CPU is
 * currently running idle, or all of the CPUs are running processes
 * with a higher priority than the one which just woke up, wake_up()
 * should not preempt any CPUs. To preempt a process, use force_preempt(). 
 * Look in os-sim.h for its prototype and parameters.
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
extern void wake_up(pcb_t *process) {
    if(alg == StaticPriority) {

        pcb_t *lowProcess = getLowestPriorityProcess(process);

        if(!hasEmptyCpus() && lowProcess != NULL) {
            int cpu_id = getCpuIdForProcess(lowProcess);
            pthread_mutex_lock(&preempt_mutex);
            preempting = 1;
            pthread_mutex_unlock(&preempt_mutex);
            process->state = PROCESS_READY;
            addReadyProcess(process);
            printf("cpu_id: %i\n", cpu_id);

            force_preempt(cpu_id);
        } else {
            process->state = PROCESS_READY;
            addReadyProcess(process);
        }
    } 
    else if(alg == MLF){
        if(process -> state != PROCESS_WAITING){
            process->state = PROCESS_READY;
            addToQueue(process,1);
        }
        else{
            print("i/o\n");
            process->state = PROCESS_READY;
            addToQueue(process,3);
        }
    }
    else {
        process->state = PROCESS_READY;
        addReadyProcess(process);
    }

}


/* The following 2 functions implement a FIFO ready queue of processes */

/* 
 * addReadyProcess adds a process to the end of a pseudo linked list (each process
 * struct contains a pointer next that you can use to chain them together)
 * it takes a pointer to a process as an argument and has no return
 */
static void addReadyProcess(pcb_t* proc) {
    // ensure no other process can access ready list while we update it
    pthread_mutex_lock(&ready_mutex);
    // add this process to the front of the ready list if preempting

    if(preempting){
        pthread_mutex_lock(&preempt_mutex);
        preempting = 0;
        pthread_mutex_unlock(&preempt_mutex);
        if (head == NULL) {
            head = proc;
            tail = proc;
            // if list was empty may need to wake up idle process
            pthread_cond_signal(&ready_empty);
        }  
        else{
            pcb_t* temp = head;
            head = proc;
            head -> next = temp;
            if(!temp -> next)
                tail = temp;
        }
    }
    else{
        if (head == NULL) {
            head = proc;
            tail = proc;
            // if list was empty may need to wake up idle process
            pthread_cond_signal(&ready_empty);
        }
        else {
            tail->next = proc;
            tail = proc;
        }

        // ensure that this proc points to NULL
        proc->next = NULL;
    }
    pthread_mutex_unlock(&ready_mutex);
}


/*
When a process wakes up you will have to check if all processors are being used and the newly woken process has a higher priority than at least one of the currently running processes, in which case you should preempt the lowest priority process currently running and run the newly woken process instead.
*/

/* 
 * getReadyProcess removes a process from the front of a pseudo linked list (each process
 * struct contains a pointer next that you can use to chain them together)
 * it takes no arguments and returns the first process in the ready queue, or NULL 
 * if the ready queue is empty
 *
 * TO-DO: handle priority scheduling 
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
static pcb_t* getReadyProcess(void) {
    pthread_mutex_lock(&ready_mutex);

    if (head == NULL) {
        pthread_mutex_unlock(&ready_mutex);
        return NULL;
    }

    // get first process to return and update head to point to next process
    pcb_t* first = head;
    head = first->next;

    // if there was no next process, list is now empty, set tail to NULL
    if (head == NULL) 
        tail = NULL;

    pthread_mutex_unlock(&ready_mutex);
    return first;
}


// MARK: - Helper methods
void setState(int cpu_id, process_state_t state) {
    pthread_mutex_lock(&current_mutex);
    pcb_t* process = getProcessForCpuId(cpu_id);

    if (process != NULL) {
        process -> state = state;
    }

    pthread_mutex_unlock(&current_mutex);
}
pcb_t* getProcessForCpuId(int cpu_id) {
    return current[cpu_id];
}
pcb_t* getLowestPriorityProcess(pcb_t* process) {
    pthread_mutex_lock(&current_mutex);
    pcb_t* lowest = process;
    int processPriority = process -> static_priority;
    int i = 0;
    while(i < cpu_count){
        if(current[i]){
            printf("current is a process with priority: %i & id: %i\n", current[i] -> static_priority, current[i] -> pid);
            printf("new process with priority: %i & id: %i\n", processPriority, process -> pid);
            if((current[i] -> static_priority) < lowest -> static_priority){
                lowest = current[i];
            }
        }
        i++;         
    }
    pthread_mutex_unlock(&current_mutex);
    if(lowest -> static_priority >= processPriority)
        return NULL;
    else{
        printf("lowest has a priority of: %i\n", lowest -> static_priority);
        return lowest;
    }
}
bool hasEmptyCpus() {
    pthread_mutex_lock(&current_mutex);
    bool hasEmpty = false;

    int i = 0;
    while(i < cpu_count){
        if(current[i] == NULL)
            hasEmpty = true;
        i++;
    }
    pthread_mutex_unlock(&current_mutex);
    return hasEmpty;

}
int getCpuIdForProcess(pcb_t* process) {
    pthread_mutex_lock(&current_mutex);
    int i = 0;
    while(i < cpu_count){
        if(current[i] == process){
            pthread_mutex_unlock(&current_mutex);
            return i;
        }
        i++;
    }
    pthread_mutex_unlock(&current_mutex);
    return -1;
}

void addToQueue(pcb_t* proc, int priority){
    pthread_mutex_lock(&ml_mutex);
    if(priority == 1){
        if(mlhead1 == NULL){
            mlhead1 = proc;
            mltail1 = proc;
            mltail1 -> next = NULL;
            pthread_cond_signal(&ml_empty);
        }
        else {
            mltail1 = proc;
            mltail1->next = NULL;
            if(!(mlhead1 -> next))
                mlhead1 -> next = mltail1;
        }
    }
    
    if(priority == 2){
        if(mlhead2 == NULL){
            mlhead2 = proc;
            mltail2 = proc;
            mltail2 -> next = NULL;
            pthread_cond_signal(&ml_empty);
        }
        else {
            mltail2 = proc;
            mltail2->next = NULL;
            if(!(mlhead2 -> next))
                mlhead2 -> next = mltail2;
        }
    }
    
    if(priority == 3){
        if(mlhead3 == NULL){
            mlhead3 = proc;
            mltail3 = proc;
            mltail3 -> next = NULL;
            pthread_cond_signal(&ml_empty);
        }
        else {
            mltail3 = proc;
            mltail3->next = NULL;
            if(!(mlhead3 -> next))
                mlhead3 -> next = mltail3;
        }
    }
    
    if(priority == 4){
        if(mlhead4 == NULL){
            mlhead4 = proc;
            mltail4 = proc;
            mltail4 -> next = NULL;
            pthread_cond_signal(&ml_empty);
        }
        else {
            mltail4 = proc;
            mltail4->next = NULL;
            if(!(mlhead4 -> next))
                mlhead4 -> next = mltail4;
        }
    }
    pthread_mutex_unlock(&ml_mutex);
}

pcb_t* removeFromQueue(int priority){
    pthread_mutex_lock(&ml_mutex);
    if(priority == 1){
        // get first process to return and update head to point to next process
        pcb_t* first = mlhead1;
        mlhead1 = first->next;
        // if there was no next process, list is now empty, set tail to NULL
        if (mlhead1 == NULL) 
            mltail1 = NULL;
        pthread_mutex_unlock(&ml_mutex);
        return first;
    }
    if(priority == 2){
        // get first process to return and update head to point to next process
        pcb_t* first = mlhead2;
        mlhead2 = first->next;
        // if there was no next process, list is now empty, set tail to NULL
        if (mlhead2 == NULL) 
            mltail2 = NULL;
        pthread_mutex_unlock(&ml_mutex);
        return first;
    }
    if(priority == 3){
        // get first process to return and update head to point to next process
        pcb_t* first = mlhead3;
        mlhead3 = first->next;
        // if there was no next process, list is now empty, set tail to NULL
        if (mlhead3 == NULL) 
            mltail3 = NULL;
        pthread_mutex_unlock(&ml_mutex);
        return first;
    }
    if(priority == 4){
        // get first process to return and update head to point to next process
        pcb_t* first = mlhead4;
        mlhead4 = first->next;
        // if there was no next process, list is now empty, set tail to NULL
        if (mlhead4 == NULL) 
            mltail4 = NULL;
        pthread_mutex_unlock(&ml_mutex);
        return first;
    }
    return NULL;
}

/*
void setTempPriority(pcb_t* proc, int queue){
    pthread_mutex_lock(&ml_mutex);
    proc -> temp_priority = queue;
    pthread_mutex_unlock(&ml_mutex);
}*/

pcb_t* getHighestMLF(){
    if(mlhead1)
        return removeFromQueue(1);
    if(mlhead2)
        return removeFromQueue(2);
    if(mlhead3)
        return removeFromQueue(3);
    if(mlhead4)
        return removeFromQueue(4);
    print("thisisnull\n");
    return NULL;
}

void print(char *text) {
    printf("%s", text);
    fflush(stdout);
}