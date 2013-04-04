
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

static struct {
	uint32_t should_run;
	uint32_t free_items;
	uint32_t qIndex;
} writer;

static struct {
	int should_run;
} reader;

static BaseSequentialStream *chp;

/* ---------------------------- */
//TODO: must be CANRxFrame later
typedef struct {
	uint16_t id;
	uint8_t  dlc;
	uint8_t  data[8];
} can_msg_t;

#define QUEUE_SIZE  6
static Mailbox      qMailbox;
static msg_t        qMailboxQueue[QUEUE_SIZE];
static can_msg_t    qData[QUEUE_SIZE];
/* ---------------------------- */

/* TODO

	- Interrupt as reader: 
	  - must use chMBFetchI(), 
	  - when queue is empty: returns RDY_TIMEOUT
	  - disable int when empty and enable it again with write_done()


*/

void test_queue_init(BaseSequentialStream *chp)
{
	(void)chp;
	chMBInit(&qMailbox, qMailboxQueue, QUEUE_SIZE);
}

void cmd_test_queue(BaseSequentialStream *_chp, int argc, char *argv[])
{
	(void)argc; (void)argv;
	chp = _chp;
	chprintf(chp, "Starting QueueTest\n");
	
	writer.should_run = 1;
	reader.should_run = 1;
	writer.qIndex = 0;
	writer.free_items = QUEUE_SIZE;

	rThread = chThdCreateStatic(waRThread, sizeof(waRThread), NORMALPRIO, 
			RThreadHandler, NULL);
	wThread = chThdCreateStatic(waWThread, sizeof(waWThread), NORMALPRIO, 
			WThreadHandler, NULL);
}

void cmd_quit_queue(BaseSequentialStream *chp, int argc, char *argv[])
{
	(void)chp; (void)argc; (void)argv;
	reader.should_run = 0;
	writer.should_run = 0;
	chThdWait(wThread);
	chThdWait(rThread);
}

can_msg_t* q_get_wslot(void)
{
	chSysLock();
	if( writer.free_items > 0 ) {
		writer.free_items--;
		chSysUnlock();
		return &qData[writer.qIndex];
	} else {
		chSysUnlock();
		//nothing free...
		return 0;
	}
}

can_msg_t* q_get_wslotI(void)
{
	chSysLockFromIsr();
	if( writer.free_items > 0 ) {
		writer.free_items--;
		chSysUnlockFromIsr();
		return &qData[writer.qIndex];
	} else {
		chSysUnlockFromIsr();
		//nothing free...
		return 0;
	}
}

//TODO: if reader is an INT, enable it when queue was empty
void q_write_done(void) 
{
	const uint32_t last_index = writer.qIndex;

	/* Increase index before post, because after post the reader can
	 * increment the free counter, which would be fatal when index ist not
	 * already incremented. */
	writer.qIndex++; 
	if(writer.qIndex >= QUEUE_SIZE)
		writer.qIndex = 0;
	/* Wakes the reader when queue was empty before */
	chMBPost(&qMailbox, (msg_t)last_index, 0);
	//chprintf(chp, "W\n");
}

void q_write_doneI(void) 
{
	const uint32_t last_index = writer.qIndex;

	/* Increase index before post, because after post the reader can
	 * increment the free counter, which would be fatal when index ist not
	 * already incremented. */
	writer.qIndex++; 
	if(writer.qIndex >= QUEUE_SIZE)
		writer.qIndex = 0;
	/* Wakes the reader when queue was empty before */
	chMBPostI(&qMailbox, (msg_t)last_index);
	//chprintf(chp, "W\n");
}

can_msg_t* q_read(void)
{
	msg_t mbstatus;
	msg_t index;

	/* Only the reader sleeps on the queue. 
	 * The writer must test the state flag of the item */
	mbstatus = chMBFetch(&qMailbox, &index, TIME_INFINITE);
	if(mbstatus == RDY_RESET) {
		chprintf(chp, "MB RESET!!!\n");
	}
	if(mbstatus == RDY_TIMEOUT) {
		chprintf(chp, "MB TIMEOUT!!!\n");
	}
	
	return &qData[index]; /* peek message out of the queue */
}

can_msg_t* q_readI(void)
{
	/* TODO */
	//test if queue is empty and disable interrupt if so 
}

void q_read_done(void)
{
	/* Marks the slot as free */
	chSysLock(); //chSysLockFromIsr();
	writer.free_items++;
	chSysUnlock(); //chSysUnlockFromIsr();
}

static msg_t RThreadHandler(void *arg) 
{
	(void)arg;
	can_msg_t *msg;

	while(reader.should_run) {
		chThdSleepMilliseconds(800);
		
		/* read next slot */
		msg = q_read();
		/* do something with the msg */
		chprintf(chp, "Rmsg: %s\n", msg->data);
		/* mark slot as free */
		q_read_done();
	}
	return 0;
}

static void canReceive(can_msg_t *msgp)
{
	int i;
	static int x = 0;
	//simulate lld_fetch
	for(i=0; i<7; i++) {
		msgp->data[i] = 'A'+x;
	}
	if(x++ > 25) x = 0;
}

static msg_t WThreadHandler(void *arg) 
{
	(void)arg;
	static can_msg_t *msgp = 0;

	while(writer.should_run) {
		chThdSleepMilliseconds(500);
		msgp = q_get_wslot();
		if(msgp) {
			canReceive(msgp);
			q_write_done();
		} //else: returned 0, so no slot aquired
		else
			chprintf(chp, "Skip\n");
	}
	return 0;
}

