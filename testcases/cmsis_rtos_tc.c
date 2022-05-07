/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-04-27     tyustli      the first version
 */

#include <rtthread.h>
#include "utest.h"
#include "cmsis_os.h"

static void osKernelInitialize_test(void)
{
    osStatus status = osErrorOS;

    status = osKernelInitialize();
    if (osOK != status)
    {
        uassert_true(RT_FALSE);
        return;
    }
    uassert_true(RT_TRUE);
}

static void osKernelStart_test(void)
{
    osStatus status = osErrorOS;

    status = osKernelStart();
    if (osOK != status)
    {
        uassert_true(RT_FALSE);
        return;
    }
    uassert_true(RT_TRUE);
}

static void osKernelRunning_test(void)
{
    char status = -1;

    status = osKernelRunning();
    if (1 != status)
    {
        uassert_true(RT_FALSE);
        return;
    }
    uassert_true(RT_TRUE);
}

static void thread_callback(void const *arg);
osThreadDef(thread_callback, osPriorityNormal, 1, 0);
static osThreadId test_tid1 = RT_NULL;

static void thread_callback(void const *arg)
{
    uassert_true(RT_TRUE);
    osDelay(10);
    if (test_tid1 != osThreadGetId())
    {
        uassert_true(RT_FALSE);
    }
    osDelay(1000);
}

static void osThread_test(void)
{
    osStatus status;

    test_tid1 = osThreadCreate(osThread(thread_callback), NULL);

    if (test_tid1 == NULL)
    {
        uassert_true(RT_FALSE);
        return;
    }

    status = osThreadSetPriority(test_tid1, osPriorityAboveNormal);
    if (osOK != status)
    {
        uassert_true(RT_FALSE);
    }

    if (osPriorityAboveNormal != osThreadGetPriority(test_tid1))
    {
        uassert_true(RT_FALSE);
    }

    osDelay(100);
    status = osThreadTerminate(test_tid1);
    if (osOK != status)
    {
        uassert_true(RT_FALSE);
        return;
    }

    uassert_true(RT_TRUE);
}

static void Timer1_Callback(void const *arg);
static void Timer2_Callback(void const *arg);
osTimerDef(Timer1, Timer1_Callback);
osTimerDef(Timer2, Timer2_Callback);
static uint32_t exec1;
static uint32_t exec2;
static char timer1_cnt = 0;
static char timer2_cnt = 0;
void Timer1_Callback(void const *arg)
{
    timer1_cnt++;
    uassert_true(RT_TRUE);
}
void Timer2_Callback(void const *arg)
{
    timer2_cnt++;
}

static void osTimer_test(void)
{
    osTimerId id1;
    osTimerId id2;

    // Create one-shoot timer
    exec1 = 1;
    id1 = osTimerCreate(osTimer(Timer1), osTimerOnce, &exec1);
    if (id1 == NULL)
    {
        uassert_true(RT_FALSE);
    }

    // Create periodic timer
    exec2 = 2;
    id2 = osTimerCreate(osTimer(Timer2), osTimerPeriodic, &exec2);
    if (id2 == NULL)
    {
        uassert_true(RT_FALSE);
    }

    osTimerStart(id1, 50);
    osTimerStart(id2, 50);
    osDelay(1050);
    uassert_true(timer2_cnt >= 20);
    osTimerDelete(id1);
    osTimerDelete(id2);
    uassert_true(RT_TRUE);
}

osMutexDef(MutexIsr);
static void osMutexDelete_test(void)
{
    osStatus status;
    osMutexId mutex_id;

    mutex_id = osMutexCreate(osMutex(MutexIsr));
    if (mutex_id == NULL)
    {
        uassert_true(RT_FALSE);
    }
    status = osMutexWait(mutex_id, 1000);
    uassert_true(osOK == status);
    status = osMutexRelease(mutex_id);
    uassert_true(osOK == status);
    status = osMutexDelete(mutex_id);
    uassert_true(osOK == status);
}

osThreadId tid_thread1;
osSemaphoreId semaphore;
osSemaphoreDef(semaphore);
void thread1(void const *argument)
{
    int32_t value;
    osDelay(20);
    value = osSemaphoreWait(semaphore, osWaitForever);
    if (value > 0)
    {
        uassert_true(RT_TRUE);
    }
}
osThreadDef(thread1, osPriorityHigh, 1, 0);
static void release_timer(void const *arg);
osTimerDef(rtimer, release_timer);
static void release_timer(void const *arg)
{
    osStatus status;

    status = osSemaphoreRelease(semaphore);
    uassert_true(status == osOK);
}
static void osSemaphore_test(void)
{
    osTimerId id1;
    semaphore = osSemaphoreCreate(osSemaphore(semaphore), 1);
    uassert_true(RT_NULL != semaphore);
    tid_thread1 = osThreadCreate(osThread(thread1), NULL);

    id1 = osTimerCreate(osTimer(rtimer), osTimerOnce, RT_NULL);
    osTimerStart(id1, 10);
    osDelay(1000);
    osSemaphoreDelete(semaphore);
    osTimerDelete(id1);
}
typedef struct
{
    uint8_t Buf[32];
    uint8_t Idx;
} MEM_BLOCK0;
osPoolDef(MemPool0, 8, MEM_BLOCK0);
static void osPool_test(void)
{
    osPoolId MemPool_Id;
    MEM_BLOCK0 *addr;
    osStatus status;

    MemPool_Id = osPoolCreate(osPool(MemPool0));
    if (MemPool_Id != NULL)
    {
        uassert_true(RT_TRUE);
        addr = (MEM_BLOCK0 *)osPoolCAlloc(MemPool_Id);
        if (addr != NULL)
        {
            rt_memset(addr, 0x5a, sizeof(MEM_BLOCK0));
            uassert_true(RT_TRUE);
            status = osPoolFree(MemPool_Id, addr);
            if (status == osOK)
            {
                uassert_true(RT_TRUE);
                return;
            }
        }
    }
    uassert_true(RT_FALSE);
}

typedef struct
{                // Message object structure
    int voltage; // AD result of measured voltage
    int current; // AD result of measured current
    int counter; // A counter value
} T_MEASSAGE;
osPoolDef(msg_mpool, 16, T_MEASSAGE); // Define memory pool
osPoolId msg_mpool;
osMessageQDef(MsgBox, 16, uint32_t); // Define message queue
osMessageQId MsgBox;
void message_send_thread(void const *argument); // forward reference
void message_recv_thread(void const *argument); // forward reference
                                                // Thread definitions
osThreadDef(message_send_thread, osPriorityNormal, 1, 0);
osThreadDef(message_recv_thread, osPriorityNormal, 1, 2000);

void message_send_thread(void const *argument)
{
    T_MEASSAGE *mptr;
    osStatus status;

    mptr = osPoolAlloc(msg_mpool); // Allocate memory for the message
    mptr->voltage = 223;           // Set the message content
    mptr->current = 26;
    mptr->counter = 120786;
    status = osMessagePut(MsgBox, (uint32_t)mptr, osWaitForever); // Send Message
    uassert_true(osOK == status);
    osDelay(5);
    mptr = osPoolAlloc(msg_mpool); // Allocate memory for the message
    mptr->voltage = 227;           // Prepare a 2nd message
    mptr->current = 12;
    mptr->counter = 170823;
    status = osMessagePut(MsgBox, (uint32_t)mptr, osWaitForever); // Send Message
    uassert_true(osOK == status);
    osThreadYield(); // Cooperative multitasking
                     // We are done here, exit this thread
}

void message_recv_thread(void const *argument)
{
    T_MEASSAGE *rptr;
    osEvent evt;

    for (;;)
    {
        evt = osMessageGet(MsgBox, osWaitForever); // wait for message
        if (evt.status == osEventMessage)
        {
            rptr = evt.value.p;
            if (120786 == rptr->counter)
            {
                uassert_true(223 == rptr->voltage);
                uassert_true(26 == rptr->current);
            }
            if (170823 == rptr->counter)
            {
                uassert_true(227 == rptr->voltage);
                uassert_true(12 == rptr->current);
            }
            osPoolFree(msg_mpool, rptr); // free memory allocated for message
        }
    }
}

void osMessage_test(void)
{
    osThreadId message_tid_thread1;                     // ID for thread 1
    osThreadId message_tid_thread2;                     // for thread 2
    msg_mpool = osPoolCreate(osPool(msg_mpool));        // create memory pool
    MsgBox = osMessageCreate(osMessageQ(MsgBox), NULL); // create msg queue
    uassert_true(RT_NULL != MsgBox);

    message_tid_thread1 = osThreadCreate(osThread(message_send_thread), NULL);
    message_tid_thread2 = osThreadCreate(osThread(message_recv_thread), NULL);
    (void)message_tid_thread1;
    osDelay(500);
    osThreadTerminate(message_tid_thread2);
}

typedef struct
{                // Mail object structure
    int voltage; // AD result of measured voltage
    int current; // AD result of measured current
    int counter; // A counter value
} T_MEAS_MAIL;

osMailQDef(mail, 16, T_MEAS_MAIL); // Define mail queue
osMailQId mail;

void mail_send_thread(void const *argument); // forward reference
void mail_recv_thread(void const *argument);

osThreadDef(mail_send_thread, osPriorityNormal, 1, 0); // thread definitions
osThreadDef(mail_recv_thread, osPriorityNormal, 1, 2000);

void mail_send_thread(void const *argument)
{
    T_MEAS_MAIL *mptr;

    mptr = osMailAlloc(mail, osWaitForever); // Allocate memory
    mptr->voltage = 223;                     // Set the mail content
    mptr->current = 17;
    mptr->counter = 120786;
    osMailPut(mail, mptr); // Send Mail
    osDelay(10);

    mptr = osMailAlloc(mail, osWaitForever); // Allocate memory
    mptr->voltage = 227;                     // Prepare 2nd mail
    mptr->current = 12;
    mptr->counter = 170823;
    osMailPut(mail, mptr); // Send Mail
    osThreadYield();       // Cooperative multitasking
                           // We are done here, exit this thread
}

void mail_recv_thread(void const *argument)
{
    T_MEAS_MAIL *rptr;
    osEvent evt;
    for (;;)
    {
        evt = osMailGet(mail, osWaitForever); // wait for mail
        if (evt.status == osEventMail)
        {
            rptr = evt.value.p;
            if (120786 == rptr->counter)
            {
                uassert_true(223 == rptr->voltage);
                uassert_true(17 == rptr->current);
            }
            if (170823 == rptr->counter)
            {
                uassert_true(227 == rptr->voltage);
                uassert_true(12 == rptr->current);
            }
            osMailFree(mail, rptr); // free memory allocated for mail
            uassert_true(RT_TRUE);
        }
    }
}

void osMail_test(void)
{
    osThreadId mail_tid_thread1; // ID for thread 1
    osThreadId mail_tid_thread2; // ID for thread 2

    mail = osMailCreate(osMailQ(mail), NULL); // create mail queue

    mail_tid_thread1 = osThreadCreate(osThread(mail_send_thread), NULL);
    mail_tid_thread2 = osThreadCreate(osThread(mail_recv_thread), NULL);
    (void)mail_tid_thread1;
    osDelay(500);
    osThreadTerminate(mail_tid_thread2);
}

static void thread_rec(void const *arg);
osThreadDef(thread_rec, osPriorityHigh, 1, 0);
static void osSignal_test(void)
{
    int32_t signals;
    osThreadId thread_id;

    thread_id = osThreadCreate(osThread(thread_rec), NULL);
    if (thread_id == NULL)
    {
        uassert_true(RT_FALSE);
    }
    else
    {
        signals = osSignalSet(thread_id, 0x02); // Send signals to the created thread
        if (signals == 0x80000000)
            uassert_true(RT_FALSE);
    }
}
static void thread_rec(void const *arg)
{
    osEvent evt;
    // wait for a signal
    evt = osSignalWait(0x02, 100);
    if (evt.status == osEventSignal)
        uassert_true(RT_TRUE);
    else
        uassert_true(RT_FALSE);
}

static rt_err_t utest_tc_init(void)
{
    return RT_EOK;
}

static rt_err_t utest_tc_cleanup(void)
{
    return RT_EOK;
}

static void testcase(void)
{
    UTEST_UNIT_RUN(osKernelInitialize_test);
    UTEST_UNIT_RUN(osKernelStart_test);
    UTEST_UNIT_RUN(osKernelRunning_test);
    UTEST_UNIT_RUN(osThread_test);
    UTEST_UNIT_RUN(osTimer_test);
    UTEST_UNIT_RUN(osMutexDelete_test);
    UTEST_UNIT_RUN(osSemaphore_test);
    UTEST_UNIT_RUN(osPool_test);
    UTEST_UNIT_RUN(osMessage_test);
    UTEST_UNIT_RUN(osMail_test);
    UTEST_UNIT_RUN(osSignal_test);
}
UTEST_TC_EXPORT(testcase, "testcases.packages.cmsis", utest_tc_init, utest_tc_cleanup, 1000);
