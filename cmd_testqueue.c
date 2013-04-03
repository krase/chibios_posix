
#include "ch.h"
#include "hal.h"
#include "chprintf.h"

#include "cmd_testqueue.h"

static msg_t RThreadHandler(void *arg);
static msg_t WThreadHandler(void *arg);

#define W_THREAD_STACK_SIZE 1024
#define R_THREAD_STACK_SIZE 1024
static Thread *wThread, *rThread;
static WORKING_AREA(waRThread, R_THREAD_STACK_SIZE);
static WORKING_AREA(waWThread, W_THREAD_STACK_SIZE);

static int w_should_run;
static int r_should_run;

static BaseSequentialStream *chp;

/* ---------------------------- */
typedef struct {
	uint16_t id;
	uint8_t  dlc;
	uint8_t  data[8];
} can_msg_t;

#define QUEUE_SIZE  6
#define MAX_W_RETRY 3
static Mailbox			qMailbox;
static msg_t			qMailboxQueue[QUEUE_SIZE];
static Semaphore		qSem;
static can_msg_t     qData[QUEUE_SIZE];
static uint32_t		qIndex;
/* ---------------------------- */


void test_queue_init(BaseSequentialStream *chp)
{
	//chprintf(chp, "SEM and Queue init\n");
	chMBInit(&qMailbox, qMailboxQueue, QUEUE_SIZE);
	chSemInit(&qSem, QUEUE_SIZE);
}

void cmd_test_queue(BaseSequentialStream *_chp, int argc, char *argv[])
{
	(void)argc; (void)argv;
	chp = _chp;
	chprintf(chp, "Starting QueueTest\n");
	w_should_run = 1;
	r_should_run = 1;
	qIndex = 0;
	rThread = chThdCreateStatic(waRThread, sizeof(waRThread), NORMALPRIO, 
			RThreadHandler, NULL);
	wThread = chThdCreateStatic(waWThread, sizeof(waWThread), NORMALPRIO, 
			WThreadHandler, NULL);
}

void cmd_quit_queue(BaseSequentialStream *chp, int argc, char *argv[])
{
	(void)chp; (void)argc; (void)argv;
	r_should_run = 0;
	w_should_run = 0;
	chThdWait(wThread);
	chThdWait(rThread);
}

int q_write(void)
{
	int i;
	static can_msg_t msg = {
		.id = 0x123,
		.dlc = 8,
		.data = {0},
	};

	for(i=0; i<7; i++) {
		msg.data[i] = 'A'+qIndex;
	}

	chprintf(chp, " Free: %d\n", chSemGetCounterI(&qSem));

	if( chSemGetCounterI(&qSem) > 0 ) { //We have space in the queue
		chSemFastWaitI(&qSem);
		qData[qIndex] = msg;
		/* Wakes the reader when queue was empty before */
		chMBPost(&qMailbox, (msg_t)qIndex, TIME_INFINITE);
		chprintf(chp, "W\n");
		qIndex++;
		if(qIndex >= QUEUE_SIZE)
			qIndex = 0;
		return 1;
	} else {
		//retry..
		return 0;
	}
}

void q_read(void)
{
	msg_t index;
	can_msg_t *msg;
	msg_t mbstatus;

	/* Only the reader sleeps in the queue. The writer must poll the semaphore */
	mbstatus = chMBFetch(&qMailbox, &index, TIME_INFINITE);
	if(mbstatus == RDY_RESET) {
		chprintf(chp, "MB RESET!!!\n");
	}
	if(mbstatus == RDY_TIMEOUT) {
		chprintf(chp, "MB TIMEOUT!!!\n");
	}
	
	msg = &qData[index];
	//do something with msg
	chprintf(chp, "Rmsg: %s\n", msg->data);
	
	/* We do not have to mark the slot as free because the semaphore
	 * ensures that the writer does not write when the queue is full */
	chSemFastSignalI(&qSem);
}

static msg_t RThreadHandler(void *arg) 
{
	(void)arg;
	while(r_should_run) {
		chThdSleepMilliseconds(800);
		//chprintf(chp, "R\n");
		q_read();
	}
	return 0;
}

static msg_t WThreadHandler(void *arg) 
{
	(void)arg;
	uint32_t count = 0;

	while(w_should_run) {
		chThdSleepMilliseconds(500);
		while(!q_write() && w_should_run && count<MAX_W_RETRY) {
			chThdSleepMilliseconds(10);
			chprintf(chp, "* retry W %d\n", count);
			q_write();
			count++;
		}
		count = 0;
	}
	return 0;
}

