#ifndef __CMD_TESTQUEUE_H__
#define __CMD_TESTQUEUE_H__

void test_queue_init(void);

void cmd_test_queue(BaseSequentialStream *_chp, int argc, char *argv[]);

void cmd_quit_queue(BaseSequentialStream *chp, int argc, char *argv[]);

#endif
