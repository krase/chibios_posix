
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
#define QUEUE_SIZE 16
static Mailbox			qMailbox;
static msg_t			MailboxQueue[QUEUE_SIZE];
static Semaphore		qSem;
/* ---------------------------- */


void test_queue_init(void)
{
}

void cmd_test_queue(BaseSequentialStream *_chp, int argc, char *argv[])
{
	(void)argc; (void)argv;
	chp = _chp;
	chprintf(chp, "Starting QueueTest\n");
	w_should_run = 1;
	r_should_run = 1;
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

static msg_t RThreadHandler(void *arg) 
{
	(void)arg;
	while(r_should_run) {
		chThdSleepMilliseconds(800);
		chprintf(chp, "R\n");
	}
	return 0;
}

static msg_t WThreadHandler(void *arg) 
{
	(void)arg;
	while(w_should_run) {
		chThdSleepMilliseconds(900);
		chprintf(chp, "W\n");
	}
	return 0;
}

