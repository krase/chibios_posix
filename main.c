#include <stdio.h>

#include "ch.h"
#include "hal.h"
#include "test.h"
#include "shell.h"
#include "chprintf.h"
#include "cmd_testqueue.h"

#define SHELL_WA_SIZE       THD_WA_SIZE(4096)
#define CONSOLE_WA_SIZE     THD_WA_SIZE(4096)
#define TEST_WA_SIZE        THD_WA_SIZE(4096)

#define cputs(msg) chMsgSend(cdtp, (msg_t)msg)

static Thread *cdtp;
static Thread *shelltp;

static void cmd_mem(BaseSequentialStream *chp, int argc, char *argv[]) {
  size_t n, size;

  (void)argv;
  if (argc > 0) {
    chprintf(chp, "Usage: mem\r\n");
    return;
  }
  n = chHeapStatus(NULL, &size);
  chprintf(chp, "core free memory : %u bytes\r\n", chCoreStatus());
  chprintf(chp, "heap fragments   : %u\r\n", n);
  chprintf(chp, "heap free total  : %u bytes\r\n", size);
}

static void cmd_threads(BaseSequentialStream *chp, int argc, char *argv[]) {
  static const char *states[] = {THD_STATE_NAMES};
  Thread *tp;

  (void)argv;
  if (argc > 0) {
    chprintf(chp, "Usage: threads\r\n");
    return;
  }
  chprintf(chp, "    addr    stack prio refs     state time\r\n");
  tp = chRegFirstThread();
  do {
    chprintf(chp, "%.8lx %.8lx %4lu %4lu %9s %lu\r\n",
            (uint32_t)tp, (uint32_t)tp->p_ctx.esp,
            (uint32_t)tp->p_prio, (uint32_t)(tp->p_refs - 1),
            states[tp->p_state], (uint32_t)tp->p_time);
    tp = chRegNextThread(tp);
  } while (tp != NULL);
}

static void cmd_test(BaseSequentialStream *chp, int argc, char *argv[]) {
  Thread *tp;

  (void)argv;
  if (argc > 0) {
    chprintf(chp, "Usage: test\r\n");
    return;
  }
  tp = chThdCreateFromHeap(NULL, TEST_WA_SIZE, chThdGetPriority(),
                           TestThread, chp);
  if (tp == NULL) {
    chprintf(chp, "out of memory\r\n");
    return;
  }
  chThdWait(tp);
}

static const ShellCommand commands[] = {
  {"mem", cmd_mem},
  {"threads", cmd_threads},
  {"test", cmd_test},
  {"queue", cmd_test_queue},
  {"quitq", cmd_quit_queue},
  {NULL, NULL}
};

static const ShellConfig shell_cfg1 = {
  (BaseSequentialStream *)&SD1,
  commands
};


/*
 * Console print server done using synchronous messages. This makes the access
 * to the C printf() thread safe and the print operation atomic among threads.
 * In this example the message is the zero terminated string itself.
 */
static msg_t console_thread(void *arg) {

  (void)arg;
  while (!chThdShouldTerminate()) {
    Thread *tp = chMsgWait();
    puts((char *)chMsgGet(tp));
    fflush(stdout);
    chMsgRelease(tp, RDY_OK);
  }
  return 0;
}

/**
 * @brief Shell termination handler.
 *
 * @param[in] id event id.
 */
static void termination_handler(eventid_t id) {

  (void)id;
  if (shelltp && chThdTerminated(shelltp)) {
    chThdWait(shelltp);
    shelltp = NULL;
    chThdSleepMilliseconds(10);
    cputs("Init: shell on SD1 terminated");
    chSysLock();
    chOQResetI(&SD1.oqueue);
    chSysUnlock();
  }
}

static EventListener sd1fel;

/**
 * @brief SD1 status change handler.
 *
 * @param[in] id event id.
 */
static void sd1_handler(eventid_t id) {
  flagsmask_t flags;

  (void)id;
  flags = chEvtGetAndClearFlags(&sd1fel);
  if ((flags & CHN_CONNECTED) && (shelltp == NULL)) {
    cputs("Init: connection on SD1");
    shelltp = shellCreate(&shell_cfg1, SHELL_WA_SIZE, NORMALPRIO + 1);
  }
  if (flags & CHN_DISCONNECTED) {
    cputs("Init: disconnection on SD1");
    chSysLock();
    chIQResetI(&SD1.iqueue);
    chSysUnlock();
  }
}

static evhandler_t fhandlers[] = {
  termination_handler,
  sd1_handler,
};

/*------------------------------------------------------------------------*
 * Simulator main.                                                        *
 *------------------------------------------------------------------------*/
int main(void) {
  EventListener tel;

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  /*
   * Serial ports (simulated) initialization.
   */
  sdStart(&SD1, NULL);

  /* Queue test initializer */
  test_queue_init(&SD1);

  /*
   * Shell manager initialization.
   */
  shellInit();
  chEvtRegister(&shell_terminated, &tel, 0);

  /*
   * Console thread started.
   */
  cdtp = chThdCreateFromHeap(NULL, CONSOLE_WA_SIZE, NORMALPRIO + 1,
                             console_thread, NULL);

  /*
   * Initializing connection/disconnection events.
   */
  cputs("Shell service started on SD1 ");
  chEvtRegister(chnGetEventSource(&SD1), &sd1fel, 1);

  /*
   * Events servicing loop.
   */
  while (!chThdShouldTerminate())
    chEvtDispatch(fhandlers, chEvtWaitOne(ALL_EVENTS));

  /*
   * Clean simulator exit.
   */
  chEvtUnregister(chnGetEventSource(&SD1), &sd1fel);
  return 0;
}
