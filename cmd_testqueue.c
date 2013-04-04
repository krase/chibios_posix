
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
typedef enum {
	ITEM_STATE_EMPTY = 0,
	ITEM_STATE_USED = 1,
} item_state_t;

typedef struct {
	uint16_t id;
	uint8_t  dlc;
	uint8_t  data[8];
} can_msg_t;

typedef struct {
	item_state_t state;
	can_msg_t msg;
} item_t;

#define QUEUE_SIZE  6
#define MAX_W_RETRY 3
static Mailbox      qMailbox;
static msg_t        qMailboxQueue[QUEUE_SIZE];
static item_t       qData[QUEUE_SIZE];
static uint32_t     qIndex;
/* ---------------------------- */


void test_queue_init(BaseSequentialStream *chp)
{
	(void)chp;
	chMBInit(&qMailbox, qMailboxQueue, QUEUE_SIZE);
}

void cmd_test_queue(BaseSequentialStream *_chp, int argc, char *argv[])
{
	uint32_t i;

	(void)argc; (void)argv;
	chp = _chp;
	chprintf(chp, "Starting QueueTest\n");
	
	w_should_run = 1;
	r_should_run = 1;
	qIndex = 0;
	for(i=0; i < QUEUE_SIZE; i++)
		qData[i].state = ITEM_STATE_EMPTY;
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

//TODO: must be CANRxFrame later
can_msg_t* q_get_wslot(void)
{
	item_t *item;

	item = &qData[qIndex];
	chSysLock(); //chSysLockFromIsr();
	if( item->state == ITEM_STATE_EMPTY ) { 
		item->state = ITEM_STATE_USED;
		chSysUnlock(); //chSysUnlockFromIsr();
		return &item->msg;
	} else {
		chSysUnlock(); //chSysUnlockFromIsr();
		//retry..
		return 0;
	}
}

void q_write_done(void) 
{
	msg_t mbstatus;

	/* Wakes the reader when queue was empty before */
	mbstatus = chMBPost(&qMailbox, (msg_t)qIndex, 0);
	//mbstatus = chMBPostI(&qMailbox, (msg_t)qIndex);
	if(mbstatus == RDY_TIMEOUT) {
		chprintf(chp, "Error: Timeout in Queue Post!!\n");
	}
	chprintf(chp, "W\n");
	qIndex++;
	if(qIndex >= QUEUE_SIZE)
		qIndex = 0;
}

msg_t q_read(item_t **item)
{
	msg_t index;
	msg_t mbstatus;

	/* Only the reader sleeps on the queue. 
	 * The writer must test the state flag of the item */
	mbstatus = chMBFetch(&qMailbox, &index, TIME_INFINITE);
	if(mbstatus == RDY_RESET) {
		chprintf(chp, "MB RESET!!!\n");
	}
	if(mbstatus == RDY_TIMEOUT) {
		chprintf(chp, "MB TIMEOUT!!!\n");
	}
	
	*item = &qData[index]; /* peek message out of the queue */
	return index;
}

void q_read_done(msg_t index)
{
	/* Marks the slot as free */
	qData[index].state = ITEM_STATE_EMPTY;
}

static msg_t RThreadHandler(void *arg) 
{
	(void)arg;
	item_t *item;
	msg_t index;

	while(r_should_run) {
		chThdSleepMilliseconds(800);
		
		/* read next slot */
		index = q_read(&item);
		/* do something with the msg */
		chprintf(chp, "Rmsg: %s\n", item->msg.data);
		/* mark slot as free */
		q_read_done(index);
	}
	return 0;
}

static msg_t WThreadHandler(void *arg) 
{
	(void)arg;
	uint32_t count = 0;
	static can_msg_t *msgp = 0;
	int i;

	while(w_should_run) {
		chThdSleepMilliseconds(500);
		msgp = q_get_wslot();
		while(!msgp && w_should_run && count<MAX_W_RETRY) {
			chThdSleepMilliseconds(10);
			chprintf(chp, "* retry W %d\n", count);
			count++;
			msgp = q_get_wslot();
		}
		if(msgp)
		{
			//simulate lld_fetch
			for(i=0; i<7; i++) {
				msgp->data[i] = 'A'+qIndex;
			}
			q_write_done();
		}
		count = 0;
	}
	return 0;
}

