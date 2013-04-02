
#include "ch.h"
#include "hal.h"
#include "chprintf.h"

#include "cmd_testqueue.h"

static void cmd_test_queue(BaseSequentialStream *chp, int argc, char *argv[])
{
	chprintf(chp, "Starting QueueTest\n");
}

