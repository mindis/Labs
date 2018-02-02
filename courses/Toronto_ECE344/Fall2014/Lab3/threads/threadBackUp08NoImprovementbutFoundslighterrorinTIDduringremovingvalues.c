#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"
#include <stdio.h> // for debugging purposes 
#include <unistd.h> 

// Running Queue
// Deleted Queue
// FIFO => A normal linked list (not sorted) 
// currently running => Head
// previously running => Tail 

#define RUNNING 0	// This is the first node in run queue 
#define WAITING 1	// Note: This is in the run queue
#define REMOVED 2  	// This is in the deleted queue 
#define BLOCKED 3   // This is in the wait queue (sleep)

/* This is the thread control block */
struct thread 
{	
	Tid tid; // from 0 to THREAD_MAX_THREADS-1, Tid defined in thread.h 
	int state; // {RUNNING, WAITING, REMOVED} 
	ucontext_t context; // to handle context switching, set as non pointer so allocated at init 
	struct thread* next; // a linked list 
};

//-----------------------------------------------------------------------
// Global variables 
//-----------------------------------------------------------------------
struct thread* headRunQueue;  
struct thread* headDeletedQueue;  


// To make sure no 2 threads have the same tid
int threadAlive[THREAD_MAX_THREADS]; // 0 if no thread, 1 if thread exists

//-----------------------------------------------------------------------
// Functions 
//-----------------------------------------------------------------------


/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void thread_stub(void (*thread_main)(void *), void *arg) // helper function for thread_create() 
{
	int enabled = interrupts_on(); 

	Tid ret;
	thread_main(arg); // call thread_main() function with arg
	ret = thread_exit(THREAD_SELF);
	// we should only get here if we are the last thread. 
	assert(ret == THREAD_NONE);
	// all threads are done, so process should exit

	interrupts_set(enabled);
	exit(0);
}


/* perform any initialization needed by your threading system */

// Note: This function is always run before any of the test functions are done 
// Note: This function is not marked, but it required for every other function 
void thread_init(void)
{
	//printf("In thread_init\n"); 
	headRunQueue = (struct thread*) malloc(sizeof(struct thread)); // allocate memory 
    headRunQueue->tid = 0; 
    headRunQueue->state = RUNNING; 
	// note: first thread is allocated by OS, so don't have to allocate context here. 
	headRunQueue->next = NULL; 
	int i = 0; 
	for (i = 0; i < THREAD_MAX_THREADS; i++)
	{
		threadAlive[i] = 0; // initialize to 0
	}
	threadAlive[headRunQueue->tid] = 1;  // update it for first thread 
	
	// Turn on interrupts 
//	register_interrupt_handler(1); 
	//interrupts_off(); // temp REMOVE THIS LINE 
	

}

//-----------------------------------------------------------------------
/* create a thread that start running the function fn(arg). Upon success, return
 * the thread identifier. On failure, return the following:
 *
 * THREAD_NOMORE: no more threads can be created. (when over THREAD_MAX_THREADS capacity) 
 * THREAD_NOMEMORY: no more memory available to create a thread stack. */  // when malloc fails 
 // Note: Do get context, but don't do set context as new thread only runs when  it receives control. 
Tid thread_create(void (*fn) (void *), void *parg)
{
	int enabled = interrupts_off(); 

	int full = 1; // initialize as full 
	int new = 0; 
	int i = 0; 
	for(i = 0; i < THREAD_MAX_THREADS; i++)
	{
		if(threadAlive[i] == 0)
		{
			full = 0; 
			new = i; 
			break; 
		}
	}
	if(full)
	{
		interrupts_set(enabled);
		return THREAD_NOMORE; 
	}
	threadAlive[new] = 1; 
	struct thread* newThread;  
	newThread = (struct thread*) malloc(sizeof(struct thread)); 
	if (newThread == NULL)
	{
		interrupts_set(enabled);
		return THREAD_NOMEMORY; 
	}
	getcontext(&(newThread->context)); 
	newThread->context.uc_mcontext.gregs[REG_RIP] = (unsigned long) thread_stub; // used thread stub 
	char* temp = (char *) malloc((THREAD_MIN_STACK+8)*sizeof(char)); 
	if(!temp)// check if stack can be allocated
	{
		interrupts_set(enabled);
		return THREAD_NOMEMORY; 
	}
	newThread->context.uc_stack.ss_sp = temp; // give it the stack 
	newThread->context.uc_stack.ss_size = THREAD_MIN_STACK; 
	newThread->context.uc_mcontext.gregs[REG_RSP] = (unsigned long) temp + THREAD_MIN_STACK + 8; // point at end of stack 

	newThread->context.uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;  // first argument
	newThread->context.uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;  // 2nd argument 
//	getcontext(&(newThread->context)); 

	newThread->tid = new; 
	newThread->state = RUNNING; 
	newThread->next = NULL; 
	struct thread* last;  
	last = headRunQueue; 
	while(last->next != NULL)
	{
		last = last->next; 
	}
	last->next = newThread; 
	int id =  newThread->tid; 
	// The signal state will be the same for the current thread, as well as the new thread when it setcontexts to it 
	// Restore signal state
	// Note: Don't need touch the interrupt mask 
	interrupts_set(enabled); 
	return id; 
}

//-----------------------------------------------------------------------

/* suspend calling thread and run the thread with identifier tid. The calling
 * thread is put in the ready queue. tid can be identifier of any available
 * thread or the following constants:
 *
 * THREAD_ANY:	   run any thread in the ready queue.
 * THREAD_SELF:    continue executing calling thread, for debugging purposes.
 *
 * Upon success, return the identifier of the thread that ran. The calling
 * thread does not see this result until it runs later. Upon failure, the
 * calling thread continues running, and returns the following:
 *
 * THREAD_INVALID: identifier tid does not correspond to a valid thread.
 * THREAD_NONE:    no more threads, other than the caller, are available to
 *		   run. this can happen is response to a call with tid set to
 *		   THREAD_ANY. */
 
Tid thread_yield(Tid want_tid)
{
	// Turn off interrupts and save initial signal state
	int enabled = interrupts_off(); 
	
	int flag = 0; // to make sure when switching between states, don't get into infinite loop 

	// If can run any thread in ready queue, 
	if (want_tid == THREAD_ANY)
	{
		struct thread* curr = headRunQueue; 
		struct thread* curr2; 
		
		if (!curr->next)
		{
			interrupts_set(enabled);
			return THREAD_NONE; 
		}
		else 
		{
			curr2 = curr; 
			while(curr2->next)
				curr2 = curr2->next; 
			headRunQueue = headRunQueue->next;
			headRunQueue->state = RUNNING; 
			curr->state = WAITING; 
			curr2->next = curr; 
			curr->next = NULL; 
			int id = headRunQueue->tid; 
			getcontext(&(curr->context)); 
			if (!flag)
			{
				flag = 1; 
				setcontext(&(headRunQueue->context)); 
			}
			else
			{
				flag = 0; 
				interrupts_set(enabled);
				return id; 
			}
		}
	}
	
	else if (want_tid == THREAD_SELF)
	{
		int id = headRunQueue->tid; 
		getcontext(&(headRunQueue->context)); 
		if (!flag)
		{
			flag = 1; 
			setcontext(&(headRunQueue->context));
		}
		else
		{
			flag = 0; 
			interrupts_set(enabled);
			return id; 
		}
	}
	
	else 
	{
		if (want_tid == headRunQueue->tid)
		{
			getcontext(&(headRunQueue->context)); 
			goto abc;
		}
		struct thread* curr = headRunQueue; 
		struct thread* curr2, *curr3, *curr4; 
		curr2 = curr; 
		while(curr2->next && curr2->next->tid != want_tid)
			curr2 = curr2->next; 
		if(!curr2->next)
		{
			interrupts_set(enabled);
			return THREAD_INVALID; 
		}
		curr3 = curr2; 
		curr2 = curr2->next; 
		curr3->next = curr3->next->next; 
		curr2->next = headRunQueue->next; 
		headRunQueue = curr2; 
		
		curr4 = headRunQueue; 
		while(curr4->next)
			curr4 = curr4->next; 
		curr4->next = curr; 
		curr->next = NULL;
		headRunQueue->state = RUNNING; 
		curr->state = WAITING; 
		int id = headRunQueue->tid; 
		getcontext(&(curr->context)); 
		abc:
			if(!flag)
			{
				flag = 1; 
				setcontext(&(headRunQueue->context)); 
			}
			else
			{
				flag = 0; 
				interrupts_set(enabled);
				return id; 
			}
	}
	interrupts_set(enabled);
	return THREAD_FAILED; 			
}
 
//-----------------------------------------------------------------------

/* destroy the thread whose identifier is tid. The calling thread continues to
 * execute and receives the result of the call. tid can be identifier of any
 * available thread or the following constants:
 *
 * THREAD_ANY:     destroy any thread except the caller.
 * THREAD_SELF:    destroy the calling thread and reclaim its resources. in this
 *		   case, the calling thread obviously doesn't run any
 *		   longer. some other ready thread is run.
 *
 * Upon success, return the identifier of the destroyed thread. A new thread
 * should be able to reuse this identifier. Upon failure, the calling thread
 * continues running, and returns the following:
 *
 * THREAD_INVALID: identifier tid does not correspond to a valid thread.
 * THREAD_NONE:	   no more threads, other than the caller, are available to
 *		   destroy, i.e., this is the last thread in the system. This
 *		   can happen in response to a call with tid set to THREAD_ANY
 *		   or THREAD_SELF. */
Tid thread_exit(Tid tid)
{
	// Turn off interrupts and save initial signal state
	int enabled = interrupts_off(); 
	
	struct thread* destroy;
	struct thread* found;
	int destroyID; 
	if (tid == THREAD_ANY)
	{
		if (headRunQueue->next == NULL)
		{
			interrupts_set(enabled);
			return THREAD_NONE; 
		}
		destroy = headRunQueue->next; 
		headRunQueue->next = headRunQueue->next->next; 
		destroyID = destroy->tid; 

		destroy->next = headDeletedQueue; 
		headDeletedQueue = destroy; 
		
		threadAlive[destroyID] = 0; 
		interrupts_set(enabled);
		return destroyID; 
	}
	else if (tid == THREAD_SELF || tid == headRunQueue->tid)
	{

		if (headRunQueue->next == NULL)
		{
			interrupts_set(enabled);
			return THREAD_NONE; 
		}
		destroy = headRunQueue; 
		headRunQueue = headRunQueue->next; 
		destroyID = destroy->tid; 

		destroy->next = headDeletedQueue; 
		headDeletedQueue = destroy; 
		
		threadAlive[destroyID] = 0; 
		setcontext(&(headRunQueue->context)); // Operating System will never call thread_exit on itself 
	}
	else
	{

		if ((tid >= THREAD_MAX_THREADS) || (tid < 0) || (threadAlive[tid] != 1)) // not equal to 1 means it can be 0 (no such item) or 2 (in wait queue) 
		{
			// If tid is out of range, or not initialize yet from threadAlive
			interrupts_set(enabled);
			return THREAD_INVALID;
		} 
		else // it corresponds to valid tid, and it exists 
		{
			found = headRunQueue; 
			while(found->next->tid != tid)
			{
				found = found->next; 
			}
			destroy = found->next;
			found->next = found->next->next; 
			destroyID = destroy->tid; 

			destroy->next = headDeletedQueue; 
			headDeletedQueue = destroy; 
			threadAlive[destroyID] = 0; 
			interrupts_set(enabled);
			return destroyID; 
		}
	}
	interrupts_set(enabled);
	return THREAD_FAILED;
}

//-------------------------------------------------------------------------------------------------------------------------

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/
/*
// THESE CODE ARE COPIED FROM ABOVE FOR EASY REFERENCE 
 #define RUNNING  0
#define WAITING 1
#define REMOVED 2 
#define BLOCKED 3 

// This is the thread control block 
struct thread 
{	
	Tid tid; // from 0 to THREAD_MAX_THREADS-1, Tid defined in thread.h 
	int state; // {RUNNING, WAITING, REMOVED} 
	ucontext_t context; // to handle context switching, set as non pointer so allocated at init 
	struct thread* next; // a linked list 
};

//-----------------------------------------------------------------------
// Global variables 
//-----------------------------------------------------------------------
struct thread* headRunQueue;  
struct thread* headDeletedQueue;  


// To make sure no 2 threads have the same tid
int threadAlive[THREAD_MAX_THREADS]; // 0 if no thread, 1 if thread exists
*/ 
 
/* This is the wait queue structure */
struct wait_queue 
{
    struct thread* headWaitQueue; // just need a pointer to elements, but that means this can be NULL!! careful
};

/* create a queue of waiting threads. initially, the queue is empty. */
struct wait_queue * wait_queue_create()
{
	// Turn off interrupts and save initial signal state
	int enabled = interrupts_off(); 
	struct wait_queue *wq;
	wq = malloc(sizeof(struct wait_queue));
	assert(wq);
	wq->headWaitQueue = NULL; // initialize to NULL 
	
	// Restore interrupts to previous condition 
	interrupts_set(enabled);
	return wq;
}

/* destroy the wait queue. be sure to check that the queue is empty when it is
 * being destroyed. */
void wait_queue_destroy(struct wait_queue *wq)
{
	int enabled = interrupts_off(); 
	
	if (wq==NULL)
	{
		interrupts_set(enabled);
		return; // don't do anything 
	}
	struct thread* remove; // just need a pointer to elements, but that means this can be NULL!! careful
	struct thread* destroy; 
	// Remove all nodes in qait queue if it is not empty 
	if (wq->headWaitQueue!=NULL)
	{
		remove = wq->headWaitQueue; 
		while (remove->next!=NULL)
		{
			
			destroy = remove->next; 
			threadAlive[remove->tid] = 0; 
			free(remove); 
			remove = destroy; 
		}
		free(remove); 
	}
	free(wq);
	interrupts_set(enabled);
	return; 
}

/* suspend calling thread and run some other thread. The calling thread is put
 * in the wait queue. Upon success, return the identifier of the thread that
 * ran. The calling thread does not see this result until it runs later. Upon
 * failure, the calling thread continues running, and returns the following:
 *
 * THREAD_INVALID: queue is invalid, e.g., it is NULL.
 * THREAD_NONE:    no more threads, other than the caller, are available to
 *		   run. */
 // Till later=> setcontext() 
Tid thread_sleep(struct wait_queue *queue)
{
	int enabled = interrupts_off(); 	
	if (queue == NULL)
	{
		interrupts_set(enabled);
		return THREAD_INVALID; 
	}
	int flag = 0; // to make sure no infinite loop 
	struct thread* curr = headRunQueue; 
	struct thread* curr2; 
	if (!curr->next)
	{
		interrupts_set(enabled);
		return THREAD_NONE; 
	}
	else 
	{ 
		curr2 = queue->headWaitQueue; 
		headRunQueue = headRunQueue->next;
		headRunQueue->state = RUNNING; 
		curr->state = BLOCKED; 
		// Make it 2 so that it doesn't count in initialization of a new thread 
		threadAlive[curr->tid] = 2;  // NOTE: This may cause problems!!!!!
		if(curr2 == NULL)
		{
			curr2 = curr; 
		}
		else
		{
			while (curr2->next != NULL)
			{
				curr2 = curr2->next; 
			}
			curr2->next = curr; 
		}
		curr->next = NULL; 
		int id = headRunQueue->tid; 
		getcontext(&(curr->context)); 
		if (!flag)
		{
			flag = 1; 
			setcontext(&(headRunQueue->context)); 
		}
		else
		{
			flag = 0; 
			interrupts_set(enabled);
			return id; 
		}
	}
	interrupts_set(enabled);
	return THREAD_FAILED;
}

/* wake up one or more threads that are suspended in the wait queue. These
 * threads are put in the ready queue. The calling thread continues to execute
 * and receives the result of the call. When "all" is 0, then one thread is
 * woken up.  When "all" is 1, all suspended threads are woken up. Wake up
 * threads in FIFO order, i.e., first thread to sleep must be woken up
 * first. The function returns the number of threads that were woken up. It can
 * return zero if there were no suspended threads in the wait queue. */
/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int thread_wakeup(struct wait_queue *queue, int all)
{
	int enabled = interrupts_off(); 	

	int numWokenUp = 0; // initialize number of threads woken up 
	if ((queue == NULL) || (queue->headWaitQueue==NULL))
	{
		interrupts_set(enabled);
		return numWokenUp; 
	}
	// Here, guaranteed at least one thread to be woken up 
	struct thread* curr2 = headRunQueue; 
	while(curr2->next!= NULL)
	{
		curr2 = curr2->next; 
	}
	// curr2 points to last element in headRunQueue
	// curr points to first element in headWait Queue
	struct thread* transfer = queue->headWaitQueue; 
	if (all == 0)
	{
		queue->headWaitQueue =  queue->headWaitQueue->next; 
		curr2->next = transfer; 
		transfer->next = NULL; 
		transfer->state = WAITING;	
		threadAlive[transfer->tid] = 1; 
		numWokenUp++; 
		interrupts_set(enabled);
		return numWokenUp;  
	}
	// wake up entire wait queue 
	else
	{
		while (queue->headWaitQueue!= NULL)
		{
			queue->headWaitQueue =  queue->headWaitQueue->next; 
			curr2->next = transfer; 
			transfer->next = NULL; 
			transfer->state = WAITING;	
			threadAlive[transfer->tid] = 1; 
			numWokenUp++; 
			curr2 = transfer; // curr2 points in lsat element in the queue 
			transfer = queue->headWaitQueue; // transfer points at the head of the waitQueue
		}
		interrupts_set(enabled);
		return numWokenUp; 
	}
	// SHOULD NEVER BE HERE 
	interrupts_set(enabled);
	return numWokenUp;
}


//---------------------------------------------------------------------------------------------------------------------------------------------

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
