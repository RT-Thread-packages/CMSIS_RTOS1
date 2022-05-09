/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-04-27     tyustli      The first version
 */

#include <rtthread.h>
#include <rtdbg.h>
#include "cmsis_os.h"

#define DEFAULT_TICK (5)
#define DEFAULT_THREAD_STACK_SIZE (512)
#define SIGNAL_ERROR_CODE (0x80000000)

#define CMSIS_MEM_ALLOC(_size) rt_malloc(_size)
#define CMSIS_MEM_FREE(_ptr) rt_free(_ptr)

#define DBG_TAG "cmsis_rtos1"
#ifdef PKG_USING_CMSIS_RTOS1_DBG
#define DBG_LVL DBG_LOG
#else
#define DBG_LVL DBG_INFO
#endif

/* cmsis to rt-thread priority map */
static const rt_uint8_t priorityArrayMap[7] =
    {
        RT_THREAD_PRIORITY_MAX - 1,
        RT_THREAD_PRIORITY_MAX - 1 - (RT_THREAD_PRIORITY_MAX / 6),
        RT_THREAD_PRIORITY_MAX - 1 - (RT_THREAD_PRIORITY_MAX / 3),
        RT_THREAD_PRIORITY_MAX / 2,
        RT_THREAD_PRIORITY_MAX / 3,
        RT_THREAD_PRIORITY_MAX / 6,
        0,
};

/* Convert from CMSIS type osPriority to RT-Thread priority number */
static rt_uint8_t makeRttPriority(osPriority priority)
{
    rt_uint8_t rttPriority = RT_THREAD_PRIORITY_MAX;

    switch (priority)
    {
    case osPriorityIdle:
        rttPriority = priorityArrayMap[0];
        break;
    case osPriorityLow:
        rttPriority = priorityArrayMap[1];
        break;
    case osPriorityBelowNormal:
        rttPriority = priorityArrayMap[2];
        break;
    case osPriorityNormal:
        rttPriority = priorityArrayMap[3];
        break;
    case osPriorityAboveNormal:
        rttPriority = priorityArrayMap[4];
        break;
    case osPriorityHigh:
        rttPriority = priorityArrayMap[5];
        break;
    case osPriorityRealtime:
        rttPriority = priorityArrayMap[6];
        break;
    default:
        break;
    }

    return rttPriority;
}

/* Convert from RT-Thread priority number to CMSIS type osPriority */
static osPriority makeCmsisPriority(rt_uint8_t rttPriority)
{
    osPriority priority = osPriorityError;

    if (rttPriority == priorityArrayMap[0])
        priority = osPriorityIdle;
    else if (rttPriority == priorityArrayMap[1])
        priority = osPriorityLow;
    else if (rttPriority == priorityArrayMap[2])
        priority = osPriorityBelowNormal;
    else if (rttPriority == priorityArrayMap[3])
        priority = osPriorityNormal;
    else if (rttPriority == priorityArrayMap[4])
        priority = osPriorityAboveNormal;
    else if (rttPriority == priorityArrayMap[5])
        priority = osPriorityHigh;
    else if (rttPriority == priorityArrayMap[6])
        rttPriority = osPriorityRealtime;

    return priority;
}

/* Determine whether we are in thread mode or handler mode. */
static int inHandlerMode(void)
{
    return rt_interrupt_get_nest() != 0;
}

/*********************** Kernel Control Functions *****************************/
/**
 * @brief  Initialize the RTOS Kernel for creating objects.
 * @retval status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osKernelInitialize shall be consistent in every CMSIS-RTOS.
 */
osStatus osKernelInitialize(void)
{
    return osOK;
}

/**
 * @brief  Start the RTOS Kernel with executing the specified thread.
 * @param  thread_def    thread definition referenced with \ref osThread.
 * @param  argument      pointer that is passed to the thread function as start argument.
 * @retval status code that indicates the execution status of the function
 * @note   MUST REMAIN UNCHANGED: \b osKernelStart shall be consistent in every CMSIS-RTOS.
 */
osStatus osKernelStart(void)
{
    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        return osErrorISR;

    return osOK;
}

/**
 * @brief  Check if the RTOS kernel is already started
 * @param  None
 * @retval (0) RTOS is not started
 *         (1) RTOS is started
 * @note  MUST REMAIN UNCHANGED: \b osKernelRunning shall be consistent in every CMSIS-RTOS.
 */
int32_t osKernelRunning(void)
{
    if (rt_thread_self())
        return 1;
    else
        return 0;
}

/**
 * @brief  Get the value of the Kernel SysTick timer
 * @param  None
 * @retval None
 * @note   MUST REMAIN UNCHANGED: \b osKernelSysTick shall be consistent in every CMSIS-RTOS.
 */
uint32_t osKernelSysTick(void)
{
    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        return 0;

    return rt_tick_get();
}

/*********************** Thread Management *****************************/
typedef struct os_thread_cb
{
    rt_thread_t thread_id;
    rt_event_t event_id;
} os_thread_cb_t;

static void thread_cleanup(struct rt_thread *tid)
{
    if (tid->user_data)
    {
        /* delete event */
        rt_event_delete(((struct os_thread_cb *)(tid->user_data))->event_id);
        /* free mem */
        CMSIS_MEM_FREE((void *)(tid->user_data));
        tid->user_data = RT_NULL;
    }
}
/**
 * @brief  Create a thread and add it to Active Threads and set it to state READY.
 * @param  thread_def    thread definition referenced with \ref osThread.
 * @param  argument      pointer that is passed to the thread function as start argument.
 * @retval thread ID for reference by other functions or NULL in case of error.
 * @note   MUST REMAIN UNCHANGED: \b osThreadCreate shall be consistent in every CMSIS-RTOS.
 */
osThreadId osThreadCreate(const osThreadDef_t *thread_def, void *argument)
{
    static rt_uint8_t thread_number = 0;
    char name[RT_NAME_MAX];
    rt_uint8_t rttPriority = 0;
    rt_uint32_t stack_size = 0;
    void (*entry)(void *parameter);
    os_thread_cb_t *thread_cb = RT_NULL;

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        goto thread_create_failed;

    if (RT_NULL == thread_def || RT_NULL == thread_def->pthread)
        goto thread_create_failed;

    rt_snprintf(name, sizeof(name), "thread%02d", thread_number++); /* thread name */
    rttPriority = makeRttPriority(thread_def->tpriority);           /* thread priority */
    entry = (void (*)(void *parameter))(thread_def->pthread);       /* thread entry */

    stack_size = thread_def->stacksize; /* thread stack size */
    if (0 == thread_def->stacksize)
        stack_size = DEFAULT_THREAD_STACK_SIZE;

    thread_cb = (void *)CMSIS_MEM_ALLOC(sizeof(struct os_thread_cb));
    if (RT_NULL == thread_cb)
        goto thread_create_failed;

    thread_cb->thread_id = rt_thread_create(name,
                                            entry,
                                            argument,
                                            stack_size,
                                            rttPriority,
                                            DEFAULT_TICK);
    thread_cb->event_id = rt_event_create(name, RT_IPC_FLAG_PRIO);

    if (RT_NULL == thread_cb->thread_id || RT_NULL == thread_cb->event_id)
        goto thread_create_failed;

    thread_cb->thread_id->user_data = (rt_ubase_t)thread_cb;
    thread_cb->thread_id->cleanup = thread_cleanup; /* when thread done. event and mem must be free */

    /* start thread */
    rt_thread_startup(thread_cb->thread_id);

    return thread_cb;
thread_create_failed:
    LOG_E("CMSIS RTOS1 %s failed", __func__);
    if (thread_cb)
    {
        if (thread_cb->thread_id)
            rt_thread_delete(thread_cb->thread_id);
        if (thread_cb->event_id)
            rt_event_delete(thread_cb->event_id);
        CMSIS_MEM_FREE(thread_cb);
    }
    return RT_NULL;
}

osStatus osThreadTerminate(osThreadId thread_id)
{
    rt_err_t result;
    osStatus status;
    rt_thread_t thread;

    /* thread_id is incorrect */
    if (RT_NULL == thread_id)
    {
        status = osErrorParameter;
        goto thread_terminate_error;
    }

    thread = thread_id->thread_id;
    /* thread_id refers to a thread that is not an active thread. thread maybe done or has terminated*/
    if (rt_object_get_type((rt_object_t)thread) != RT_Object_Class_Thread)
    {
        status = osErrorResource;
        goto thread_terminate_error;
    }

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto thread_terminate_error;
    }

    result = rt_thread_control(thread, RT_THREAD_CTRL_CLOSE, RT_NULL);
    if (result != RT_EOK)
    {
        status = osErrorOS;
        goto thread_terminate_error;
    }

    return osOK;
thread_terminate_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}

/**
 * @brief  Pass control to next thread that is in state \b READY.
 * @retval status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osThreadYield shall be consistent in every CMSIS-RTOS.
 */
osStatus osThreadYield(void)
{
    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        return osErrorISR;

    rt_thread_yield();

    return osOK;
}

/**
 * @brief  Return the thread ID of the current running thread.
 * @retval thread ID for reference by other functions or NULL in case of error.
 * @note   MUST REMAIN UNCHANGED: \b osThreadGetId shall be consistent in every CMSIS-RTOS.
 */
osThreadId osThreadGetId(void)
{
    rt_thread_t thread;

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        return RT_NULL;

    thread = rt_thread_self();

    return (osThreadId)(thread->user_data);
}

/**
 * @brief   Change priority of an active thread.
 * @param   thread_id     thread ID obtained by \ref osThreadCreate or \ref osThreadGetId.
 * @param   priority      new priority value for the thread function.
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osThreadSetPriority shall be consistent in every CMSIS-RTOS.
 */
osStatus osThreadSetPriority(osThreadId thread_id, osPriority priority)
{
    osStatus status;
    rt_err_t result;
    rt_uint8_t rttPriority = 0;
    rt_thread_t thread;

    /* thread_id is incorrect */
    if (RT_NULL == thread_id)
    {
        status = osErrorParameter;
        goto set_pri_error;
    }

    /* incorrect priority value */
    if (osPriorityError == priority)
    {
        status = osErrorValue;
        goto set_pri_error;
    }

    thread = thread_id->thread_id;
    /* thread_id refers to a thread that is not an active thread. thread maybe done or has terminated*/
    if (rt_object_get_type((rt_object_t)thread) != RT_Object_Class_Thread)
    {
        status = osErrorResource;
        goto set_pri_error;
    }

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto set_pri_error;
    }

    rttPriority = makeRttPriority(priority);
    result = rt_thread_control(thread, RT_THREAD_CTRL_CHANGE_PRIORITY, &rttPriority);
    if (result != RT_EOK)
    {
        status = osErrorOS;
        goto set_pri_error;
    }

    return osOK;
set_pri_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}

/**
 * @brief   Get current priority of an active thread.
 * @param   thread_id     thread ID obtained by \ref osThreadCreate or \ref osThreadGetId.
 * @retval  current priority value of the thread function.
 * @note   MUST REMAIN UNCHANGED: \b osThreadGetPriority shall be consistent in every CMSIS-RTOS.
 */
osPriority osThreadGetPriority(osThreadId thread_id)
{
    rt_thread_t thread;

    if (RT_NULL == thread_id)
        goto get_pri_error;

    thread = thread_id->thread_id;
    /* thread_id refers to a thread that is not an active thread. thread maybe done or has terminated*/
    if (rt_object_get_type((rt_object_t)thread) != RT_Object_Class_Thread)
        goto get_pri_error;

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        goto get_pri_error;

    return makeCmsisPriority(thread->current_priority);
get_pri_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__);
    return osPriorityError;
}

/*********************** Generic Wait Functions *******************************/
/**
 * @brief   Wait for Timeout (Time Delay)
 * @param   millisec      time delay value
 * @retval  status code that indicates the execution status of the function.
 */
osStatus osDelay(uint32_t millisec)
{
    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        return osErrorISR;

    rt_thread_mdelay(millisec);

    return osOK;
}

#if (defined(osFeature_Wait) && (osFeature_Wait != 0)) /* Generic Wait available */
/**
 * @brief  Wait for Signal, Message, Mail, or Timeout
 * @param   millisec  timeout value or 0 in case of no time-out
 * @retval  event that contains signal, message, or mail information or error code.
 * @note   MUST REMAIN UNCHANGED: \b osWait shall be consistent in every CMSIS-RTOS.
 */
osEvent osWait(uint32_t millisec)
{
    osEvent event;

    event.status = osErrorOS;
    LOG_E("CMSIS RTOS1 %s failed. this function not implement", __func__);

    return event;
}

#endif /* Generic Wait available */

/***********************  Timer Management Functions ***************************/
/**
 * @brief  Create a timer.
 * @param  timer_def     timer object referenced with \ref osTimer.
 * @param  type          osTimerOnce for one-shot or osTimerPeriodic for periodic behavior.
 * @param  argument      argument to the timer call back function.
 * @retval  timer ID for reference by other functions or NULL in case of error.
 * @note   MUST REMAIN UNCHANGED: \b osTimerCreate shall be consistent in every CMSIS-RTOS.
 */
osTimerId osTimerCreate(const osTimerDef_t *timer_def, os_timer_type type, void *argument)
{
    rt_timer_t timer;
    static rt_uint16_t timer_number = 0U;
    rt_uint8_t flag = RT_TIMER_FLAG_SOFT_TIMER;
    char name[RT_NAME_MAX];
    void (*timeout_callback)(void *parameter);

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        goto timer_create_error;

    if (RT_NULL == timer_def || RT_NULL == timer_def->ptimer)
        goto timer_create_error;

    if (osTimerPeriodic == type)
        flag |= RT_TIMER_FLAG_PERIODIC;
    else if (osTimerOnce == type)
        flag |= RT_TIMER_FLAG_ONE_SHOT;
    else
        goto timer_create_error;

    rt_snprintf(name, sizeof(name), "timer%02d", timer_number++);      /* timer name */
    timeout_callback = (void (*)(void *parameter))(timer_def->ptimer); /* timeout callback */

    timer = rt_timer_create(name,
                            timeout_callback,
                            argument,
                            0,
                            flag);
    if (RT_NULL == timer)
        goto timer_create_error;

    return (osTimerId)timer;

timer_create_error:
    LOG_E("CMSIS RTOS1 %s failed", __func__);
    return RT_NULL;
}

/**
 * @brief  Start or restart a timer.
 * @param  timer_id      timer ID obtained by \ref osTimerCreate.
 * @param  millisec      time delay value of the timer.
 * @retval  status code that indicates the execution status of the function
 * @note   MUST REMAIN UNCHANGED: \b osTimerStart shall be consistent in every CMSIS-RTOS.
 */
osStatus osTimerStart(osTimerId timer_id, uint32_t millisec)
{
    rt_err_t result;
    rt_tick_t ticks;
    osStatus status;

    /* timer_id is incorrect */
    if ((RT_NULL == timer_id))
    {
        status = osErrorParameter;
        goto timer_start_error;
    }

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto timer_start_error;
    }

    ticks = rt_tick_from_millisecond(millisec);
    result = rt_timer_control((rt_timer_t)timer_id, RT_TIMER_CTRL_SET_TIME, &ticks);
    if (result != RT_EOK)
    {
        status = osErrorOS;
        goto timer_start_error;
    }

    result = rt_timer_start((rt_timer_t)timer_id);
    if (result != RT_EOK)
    {
        status = osErrorOS;
        goto timer_start_error;
    }

    return osOK;
timer_start_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}

/**
 * @brief  Stop a timer.
 * @param  timer_id      timer ID obtained by \ref osTimerCreate
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osTimerStop shall be consistent in every CMSIS-RTOS.
 */
osStatus osTimerStop(osTimerId timer_id)
{
    rt_err_t result;
    rt_uint32_t timer_status;
    osStatus status;

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto timer_stop_error;
    }

    /* timer_id is incorrect */
    if (RT_NULL == timer_id)
    {
        status = osErrorParameter;
        goto timer_stop_error;
    }

    /* the timer is not started */
    rt_timer_control((rt_timer_t)timer_id, RT_TIMER_CTRL_GET_STATE, &timer_status);
    if (RT_TIMER_FLAG_ACTIVATED != timer_status)
    {
        status = osErrorResource;
        goto timer_stop_error;
    }

    result = rt_timer_stop((rt_timer_t)timer_id);
    if (result != RT_EOK)
    {
        status = osErrorOS;
        goto timer_stop_error;
    }

    return osOK;
timer_stop_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}

/**
 * @brief  Delete a timer.
 * @param  timer_id      timer ID obtained by \ref osTimerCreate
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osTimerDelete shall be consistent in every CMSIS-RTOS.
 */
osStatus osTimerDelete(osTimerId timer_id)
{
    rt_err_t result;
    osStatus status;

    /* timer_id is incorrect. */
    if (RT_NULL == timer_id)
    {
        status = osErrorParameter;
        goto timer_delete_error;
    }

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto timer_delete_error;
    }

    result = rt_timer_delete((rt_timer_t)timer_id);
    if (RT_EOK != result)
    {
        status = osErrorOS;
        goto timer_delete_error;
    }

    return osOK;
timer_delete_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}

/***************************  Signal Management ********************************/
/**
 * @brief  Set the specified Signal Flags of an active thread.
 * @param  thread_id     thread ID obtained by \ref osThreadCreate or \ref osThreadGetId.
 * @param  signals       specifies the signal flags of the thread that should be set.
 * @retval previous signal flags of the specified thread or 0x80000000 in case of incorrect parameters.
 * @note   MUST REMAIN UNCHANGED: \b osSignalSet shall be consistent in every CMSIS-RTOS.
 */
int32_t osSignalSet(osThreadId thread_id, int32_t signal)
{
    rt_event_t event;
    int32_t ret = SIGNAL_ERROR_CODE;
    rt_err_t result;

    if (RT_NULL == thread_id)
        goto signal_set_error;

    event = thread_id->event_id;

    /* previous signal flags of the specified thread */
    rt_enter_critical();
    ret = event->set;
    rt_exit_critical();

    result = rt_event_send(event, signal);
    if (RT_EOK != result)
    {
        ret = SIGNAL_ERROR_CODE;
        goto signal_set_error;
    }

    return ret;
signal_set_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, ret);
    return SIGNAL_ERROR_CODE;
}
/**
 * @brief  Clear the specified Signal Flags of an active thread.
 * @param  thread_id  thread ID obtained by \ref osThreadCreate or \ref osThreadGetId.
 * @param  signals    specifies the signal flags of the thread that shall be cleared.
 * @retval  previous signal flags of the specified thread or 0x80000000 in case of incorrect parameters.
 * @note   MUST REMAIN UNCHANGED: \b osSignalClear shall be consistent in every CMSIS-RTOS.
 */
int32_t osSignalClear(osThreadId thread_id, int32_t signal)
{
    rt_event_t event;
    int32_t ret = SIGNAL_ERROR_CODE;

    if (RT_NULL == thread_id)
        goto signal_clear_error;

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
        goto signal_clear_error;

    event = thread_id->event_id;

    /* previous signal flags of the specified thread */
    rt_enter_critical();
    ret = event->set;
    rt_exit_critical();

    rt_event_control(event, RT_IPC_CMD_RESET, RT_NULL);

    return ret;
signal_clear_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, ret);
    return SIGNAL_ERROR_CODE;
}

/**
 * @brief  Wait for one or more Signal Flags to become signaled for the current \b RUNNING thread.
 * @param  signals   wait until all specified signal flags set or 0 for any single signal flag.
 * @param  millisec  timeout value or 0 in case of no time-out.
 * @retval  event flag information or error code.
 * @note   MUST REMAIN UNCHANGED: \b osSignalWait shall be consistent in every CMSIS-RTOS.
 */
osEvent osSignalWait(int32_t signals, uint32_t millisec)
{
    osEvent event;
    rt_event_t recv_event;
    osThreadId thread_cb;
    rt_uint32_t recved;
    rt_uint32_t ticks = 0;
    rt_err_t result;

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
    {
        event.status = osErrorISR;
        goto signal_wait_error;
    }

    /* get receive event */
    thread_cb = osThreadGetId();
    recv_event = thread_cb->event_id;

    /* get timeout */
    if (osWaitForever == millisec)
        ticks = RT_WAITING_FOREVER;
    else if (0U != millisec)
        ticks = rt_tick_from_millisecond(millisec);

    if (signals != 0)
        result = rt_event_recv(recv_event, signals, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, ticks, &recved);
    else
        result = rt_event_recv(recv_event, signals, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, ticks, &recved);

    if (-RT_ETIMEOUT == result)
    {
        event.status = osEventTimeout;
        goto signal_wait_error;
    }
    else if (RT_EOK != result)
    {
        event.status = osErrorOS;
        goto signal_wait_error;
    }

    event.status = osEventSignal;
    return event;
signal_wait_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, event.status);
    return event;
}

/****************************  Mutex Management ********************************/
/**
 * @brief  Create and Initialize a Mutex object
 * @param  mutex_def     mutex definition referenced with \ref osMutex.
 * @retval  mutex ID for reference by other functions or NULL in case of error.
 * @note   MUST REMAIN UNCHANGED: \b osMutexCreate shall be consistent in every CMSIS-RTOS.
 */
osMutexId osMutexCreate(const osMutexDef_t *mutex_def)
{
#ifdef RT_USING_MUTEX
    static rt_uint16_t mutex_cnt = 0U;
    char name[RT_NAME_MAX];
    rt_mutex_t mutex;

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
        goto mutex_create_failed;

    rt_snprintf(name, sizeof(name), "mutex%02d", mutex_cnt++); /* mutex name */
    mutex = rt_mutex_create(name, RT_IPC_FLAG_PRIO);
    if (RT_NULL == mutex)
        goto mutex_create_failed;

    return (osMutexId)mutex;
mutex_create_failed:
    LOG_E("CMSIS RTOS1 %s failed", __func__);
    return RT_NULL;
#else  /* not define RT_USING_MUTEX */
    return RT_NULL;
#endif /* RT_USING_MUTEX */
}

/**
 * @brief Wait until a Mutex becomes available
 * @param mutex_id      mutex ID obtained by \ref osMutexCreate.
 * @param millisec      timeout value or 0 in case of no time-out.
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osMutexWait shall be consistent in every CMSIS-RTOS.
 */
osStatus osMutexWait(osMutexId mutex_id, uint32_t millisec)
{
    rt_err_t result;
    osStatus status;
    rt_uint32_t ticks = 0;

    /* the parameter mutex_id is incorrect */
    if (RT_NULL == mutex_id)
    {
        status = osErrorParameter;
        goto mutex_wait_error;
    }

    /* the mutex could not be obtained when no timeout was specified */
    if (0 == millisec)
    {
        status = osErrorResource;
        goto mutex_wait_error;
    }

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto mutex_wait_error;
    }

    rt_enter_critical();
    if (((rt_mutex_t)mutex_id)->owner == rt_thread_self())
    {
        rt_exit_critical();
        status = osErrorOS;
        goto mutex_wait_error;
    }
    rt_exit_critical();

    if (osWaitForever == millisec)
        ticks = RT_WAITING_FOREVER;
    else if (0U != millisec)
        ticks = rt_tick_from_millisecond(millisec);

    result = rt_mutex_take((rt_mutex_t)mutex_id, ticks);
    if (-RT_ETIMEOUT == result)
    {
        status = osErrorTimeoutResource;
        goto mutex_wait_error;
    }
    else if (RT_EOK != result)
    {
        status = osErrorOS;
        goto mutex_wait_error;
    }

    return osOK;
mutex_wait_error:
    LOG_E("CMSIS RTOS1 %s failed", __func__);
    return status;
}

/**
 * @brief Release a Mutex that was obtained by \ref osMutexWait
 * @param mutex_id      mutex ID obtained by \ref osMutexCreate.
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osMutexRelease shall be consistent in every CMSIS-RTOS.
 */
osStatus osMutexRelease(osMutexId mutex_id)
{
    rt_err_t result;
    osStatus status;

    /* the parameter mutex_id is incorrect */
    if (RT_NULL == mutex_id)
    {
        status = osErrorParameter;
        goto mutex_release_error;
    }

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto mutex_release_error;
    }

    /*  the mutex was not obtained before */
    if (rt_object_get_type(&((rt_mutex_t)mutex_id)->parent.parent) != RT_Object_Class_Mutex)
    {
        status = osErrorResource;
        goto mutex_release_error;
    }

    result = rt_mutex_release((rt_mutex_t)mutex_id);
    if (RT_EOK != result)
    {
        status = osErrorOS;
        goto mutex_release_error;
    }

    return osOK;
mutex_release_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}

/**
 * @brief Delete a Mutex
 * @param mutex_id  mutex ID obtained by \ref osMutexCreate.
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osMutexDelete shall be consistent in every CMSIS-RTOS.
 */
osStatus osMutexDelete(osMutexId mutex_id)
{
    osStatus status;
    rt_err_t result;

    /* Check parameters */
    if (RT_NULL == mutex_id)
    {
        status = osErrorParameter;
        goto mutex_delete_error;
    }

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto mutex_delete_error;
    }

    /*  all tokens have already been released */
    if (rt_object_get_type(&((rt_mutex_t)mutex_id)->parent.parent) != RT_Object_Class_Mutex)
    {
        status = osErrorResource;
        goto mutex_delete_error;
    }

    result = rt_mutex_delete((rt_mutex_t)mutex_id);
    if (RT_EOK != result)
    {
        status = osErrorOS;
        goto mutex_delete_error;
    }

    return osOK;
mutex_delete_error:
    LOG_E("CMSIS RTOS1 %s failed error code is :%d", __func__, status);
    return status;
}

/********************  Semaphore Management Functions **************************/

#if (defined(osFeature_Semaphore) && (osFeature_Semaphore != 0))
/**
 * @brief Create and Initialize a Semaphore object used for managing resources
 * @param semaphore_def semaphore definition referenced with \ref osSemaphore.
 * @param count         number of available resources.
 * @retval  semaphore ID for reference by other functions or NULL in case of error.
 * @note   MUST REMAIN UNCHANGED: \b osSemaphoreCreate shall be consistent in every CMSIS-RTOS.
 */
osSemaphoreId osSemaphoreCreate(const osSemaphoreDef_t *semaphore_def, int32_t count)
{
#ifdef RT_USING_SEMAPHORE
    static rt_uint16_t sem_cnt = 0U;
    char name[RT_NAME_MAX];
    rt_sem_t sem;

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
        goto sem_create_error;

    if (count < 0 || count > osFeature_Semaphore)
        goto sem_create_error;

    rt_snprintf(name, sizeof(name), "sem%02d", sem_cnt++);
    sem = rt_sem_create(name, count, RT_IPC_FLAG_PRIO);
    if (RT_NULL == sem)
        goto sem_create_error;

    return (osSemaphoreId)sem;
sem_create_error:
    LOG_E("CMSIS RTOS1 %s failed error", __func__);
    return RT_NULL;
#else  /* not defined RT_USING_SEMAPHORE */
    return RT_NULL;
#endif /* RT_USING_SEMAPHORE */
}

/* get semaphore cnt */
static int32_t _osSemaphoreGetCount(osSemaphoreId semaphore_id)
{
    rt_sem_t sem_cb = (rt_sem_t)semaphore_id;

    /* Check parameters */
    if ((RT_NULL == sem_cb) || (rt_object_get_type(&sem_cb->parent.parent) != RT_Object_Class_Semaphore))
        return 0U;

    return sem_cb->value;
}
/**
 * @brief Wait until a Semaphore token becomes available
 * @param  semaphore_id  semaphore object referenced with \ref osSemaphore.
 * @param  millisec      timeout value or 0 in case of no time-out.
 * @retval  number of available tokens, or -1 in case of incorrect parameters.
 * @note   MUST REMAIN UNCHANGED: \b osSemaphoreWait shall be consistent in every CMSIS-RTOS.
 */
int32_t osSemaphoreWait(osSemaphoreId semaphore_id, uint32_t millisec)
{
    rt_err_t result;
    rt_int32_t ticks = 0;

    if (RT_NULL == semaphore_id)
        goto sem_take_error;

    /* when millisec is set to osWaitForever the function will wait for an infinite time until the Semaphore token becomes available. */
    if (osWaitForever == millisec)
        ticks = RT_WAITING_FOREVER;
    else if (0U != millisec)
        ticks = rt_tick_from_millisecond(millisec);

    /* when millisec is 0, the function returns instantly */
    if (0 == millisec)
        result = rt_sem_trytake((rt_sem_t)semaphore_id);
    else
        result = rt_sem_take((rt_sem_t)semaphore_id, ticks);

    if (RT_EOK != result)
        goto sem_take_error;

    return _osSemaphoreGetCount(semaphore_id);
sem_take_error:
    LOG_E("CMSIS RTOS1 %s failed ", __func__);
    return 0; /* If 0 is returned, then no semaphore was available. */
}

/**
 * @brief Release a Semaphore token
 * @param  semaphore_id  semaphore object referenced with \ref osSemaphore.
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osSemaphoreRelease shall be consistent in every CMSIS-RTOS.
 */
osStatus osSemaphoreRelease(osSemaphoreId semaphore_id)
{
    rt_err_t result;
    osStatus status;

    /* the parameter semaphore_id is incorrect. */
    if (RT_NULL == semaphore_id)
    {
        status = osErrorParameter;
        goto sem_release_error;
    }

    /* all tokens have already been released. */
    if (0 == _osSemaphoreGetCount(semaphore_id))
    {
        status = osErrorResource;
        goto sem_release_error;
    }

    result = rt_sem_release((rt_sem_t)semaphore_id);
    if (RT_EOK != result)
    {
        status = osErrorOS;
        goto sem_release_error;
    }

    return osOK;
sem_release_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}

/**
 * @brief Delete a Semaphore
 * @param  semaphore_id  semaphore object referenced with \ref osSemaphore.
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osSemaphoreDelete shall be consistent in every CMSIS-RTOS.
 */
osStatus osSemaphoreDelete(osSemaphoreId semaphore_id)
{
    rt_err_t result;
    osStatus status;

    /*  the parameter semaphore_id is incorrect. */
    if (RT_NULL == semaphore_id)
    {
        status = osErrorParameter;
        goto sem_delete_error;
    }

    /* Cannot be called from Interrupt Service Routines */
    if (inHandlerMode())
    {
        status = osErrorISR;
        goto sem_delete_error;
    }

    /* the semaphore object could not be deleted. */
    if (rt_object_get_type((rt_object_t) & (((rt_sem_t)semaphore_id)->parent)) != RT_Object_Class_Semaphore)
    {
        status = osErrorResource;
        goto sem_delete_error;
    }

    result = rt_sem_delete((rt_sem_t)semaphore_id);
    if (RT_EOK != result)
    {
        status = osErrorOS;
        goto sem_delete_error;
    }

    return osOK;
sem_delete_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}
#endif /* Use Semaphores */

/*******************   Memory Pool Management Functions  ***********************/

#if (defined(osFeature_Pool) && (osFeature_Pool != 0))
/**
 * @brief Create and Initialize a memory pool
 * @param  pool_def      memory pool definition referenced with \ref osPool.
 * @retval  memory pool ID for reference by other functions or NULL in case of error.
 * @note   MUST REMAIN UNCHANGED: \b osPoolCreate shall be consistent in every CMSIS-RTOS.
 */
osPoolId osPoolCreate(const osPoolDef_t *pool_def)
{
    static rt_uint16_t mp_cnt = 0U;
    char name[RT_NAME_MAX];
    rt_mp_t mempool;
    rt_size_t block_size;

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
        goto mp_create_error;

    if (RT_NULL == pool_def || 0 == pool_def->pool_sz || 0 == pool_def->item_sz)
        goto mp_create_error;

    rt_snprintf(name, sizeof(name), "mp%02d", mp_cnt++);     /* name */
    block_size = RT_ALIGN(pool_def->item_sz, RT_ALIGN_SIZE); /* pool size */

    mempool = rt_mp_create(name, pool_def->pool_sz, block_size);
    if (RT_NULL == mempool)
        goto mp_create_error;

    return (osPoolId)mempool;
mp_create_error:
    LOG_E("CMSIS RTOS1 %s failed", __func__);
    return RT_NULL;
}

/**
 * @brief Allocate a memory block from a memory pool
 * @param pool_id       memory pool ID obtain referenced with \ref osPoolCreate.
 * @retval  address of the allocated memory block or NULL in case of no memory available.
 * @note   MUST REMAIN UNCHANGED: \b osPoolAlloc shall be consistent in every CMSIS-RTOS.
 */
void *osPoolAlloc(osPoolId pool_id)
{
    /* Check parameters */
    if (RT_NULL == pool_id)
        return RT_NULL;

    return rt_mp_alloc((rt_mp_t)pool_id, RT_WAITING_FOREVER);
}

/**
 * @brief Allocate a memory block from a memory pool and set memory block to zero
 * @param  pool_id       memory pool ID obtain referenced with \ref osPoolCreate.
 * @retval  address of the allocated memory block or NULL in case of no memory available.
 * @note   MUST REMAIN UNCHANGED: \b osPoolCAlloc shall be consistent in every CMSIS-RTOS.
 */
void *osPoolCAlloc(osPoolId pool_id)
{
    void *p = osPoolAlloc(pool_id);

    if (p != NULL)
        rt_memset(p, 0, ((rt_mp_t)pool_id)->block_size);

    return p;
}

/**
 * @brief Return an allocated memory block back to a specific memory pool
 * @param  pool_id       memory pool ID obtain referenced with \ref osPoolCreate.
 * @param  block         address of the allocated memory block that is returned to the memory pool.
 * @retval  status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osPoolFree shall be consistent in every CMSIS-RTOS.
 */
osStatus osPoolFree(osPoolId pool_id, void *block)
{
    osStatus status;
    /* Check parameters */
    if (NULL == pool_id)
    {
        status = osErrorParameter;
        goto pool_free_error;
    }

    if (NULL == block)
    {
        status = osErrorParameter;
        goto pool_free_error;
    }

    rt_mp_free(block);
    block = RT_NULL;

    return osOK;
pool_free_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}
#endif /* Use Memory Pool Management */

/*******************   Message Queue Management Functions  *********************/

#if (defined(osFeature_MessageQ) && (osFeature_MessageQ != 0)) /* Use Message Queues */
/**
 * @brief Create and Initialize a Message Queue
 * @param queue_def     queue definition referenced with \ref osMessageQ.
 * @param  thread_id     thread ID (obtained by \ref osThreadCreate or \ref osThreadGetId) or NULL.
 * @retval  message queue ID for reference by other functions or NULL in case of error.
 * @note   MUST REMAIN UNCHANGED: \b osMessageCreate shall be consistent in every CMSIS-RTOS.
 */
osMessageQId osMessageCreate(const osMessageQDef_t *queue_def, osThreadId thread_id)
{
#ifdef RT_USING_MAILBOX
    (void)thread_id;
    static rt_uint16_t mailbox_cnt = 0U;
    char name[RT_NAME_MAX];
    rt_mailbox_t mailbox;

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
        goto message_create_error;

    /* param error */
    if (RT_NULL == queue_def || 0 == queue_def->queue_sz || sizeof(void *) != queue_def->item_sz)
        goto message_create_error;

    rt_snprintf(name, sizeof(name), "mailbox%02d", mailbox_cnt++);
    mailbox = rt_mb_create(name, queue_def->queue_sz, RT_IPC_FLAG_PRIO);
    if (mailbox == RT_NULL)
        goto message_create_error;

    return (osMessageQId)mailbox;
message_create_error:
    LOG_E("CMSIS RTOS1 %s failed", __func__);
    return RT_NULL;
#else  /* RT_USING_MAILBOX */
    return RT_NULL;
#endif /* not define RT_USING_MAILBOX */
}
/**
 * @brief Put a Message to a Queue.
 * @param  queue_id  message queue ID obtained with \ref osMessageCreate.
 * @param  info      message information.
 * @param  millisec  timeout value or 0 in case of no time-out.
 * @retval status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osMessagePut shall be consistent in every CMSIS-RTOS.
 */
osStatus osMessagePut(osMessageQId queue_id, uint32_t info, uint32_t millisec)
{
    rt_err_t result;
    osStatus status;
    rt_mailbox_t mb_cb = (rt_mailbox_t)queue_id;
    rt_tick_t ticks = 0;

    if (RT_NULL == queue_id)
    {
        /* a parameter is invalid or outside of a permitted range */
        status = osErrorParameter;
        goto message_put_error;
    }

    if (osWaitForever == millisec)
        ticks = RT_WAITING_FOREVER;
    else if (0U != millisec)
        ticks = rt_tick_from_millisecond(millisec);

    /* when millisec is 0, the function returns instantly */
    if (0 == millisec)
        result = rt_mb_send(mb_cb, info);
    else
        result = rt_mb_send_wait(mb_cb, info, ticks);

    if (-RT_EFULL == result)
    {
        /* no memory in the queue was available */
        status = osErrorResource;
        goto message_put_error;
    }
    else if (-RT_ETIMEOUT == result)
    {
        /* no memory in the queue was available during the given time limit. */
        status = osErrorTimeoutResource;
        goto message_put_error;
    }
    if (RT_EOK != result)
    {
        status = osErrorOS;
        goto message_put_error;
    }

    return osOK;
message_put_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}
/**
 * @brief Get a Message or Wait for a Message from a Queue.
 * @param  queue_id  message queue ID obtained with \ref osMessageCreate.
 * @param  millisec  timeout value or 0 in case of no time-out.
 * @retval event information that includes status code.
 * @note   MUST REMAIN UNCHANGED: \b osMessageGet shall be consistent in every CMSIS-RTOS.
 */
osEvent osMessageGet(osMessageQId queue_id, uint32_t millisec)
{
    osEvent event;
    rt_err_t result;
    rt_tick_t ticks = 0;

    if (RT_NULL == queue_id)
    {
        /* a parameter is invalid or outside of a permitted range */
        event.status = osErrorParameter;
        goto message_get_error;
    }

    event.def.message_id = queue_id;
    event.value.v = 0;

    if (osWaitForever == millisec)
        ticks = RT_WAITING_FOREVER;
    else if (0U != millisec)
        ticks = rt_tick_from_millisecond(millisec);

    result = rt_mb_recv((rt_mailbox_t)queue_id, &(event.value.v), ticks);
    if (-RT_ETIMEOUT == result)
    {
        /* no message has arrived during the given timeout period */
        event.status = osEventTimeout;
        goto message_get_error;
    }
    else if (RT_EOK != result)
    {
        event.status = osErrorOS;
        goto message_get_error;
    }

    /* message received, value.p contains the pointer to message */
    event.status = osEventMessage;
    return event;
message_get_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, event.status);
    return event;
}
#endif /* Use Message Queues */

/********************   Mail Queue Management Functions  ***********************/
#if (defined(osFeature_MailQ) && (osFeature_MailQ != 0)) /* Use Mail Queues */
typedef struct os_mailQ_cb
{
    rt_mp_t mp_id;
    rt_mailbox_t mb_id;
} os_mailQ_cb_t;
/**
 * @brief Create and Initialize mail queue
 * @param  queue_def     reference to the mail queue definition obtain with \ref osMailQ
 * @param   thread_id     thread ID (obtained by \ref osThreadCreate or \ref osThreadGetId) or NULL.
 * @retval mail queue ID for reference by other functions or NULL in case of error.
 * @note   MUST REMAIN UNCHANGED: \b osMailCreate shall be consistent in every CMSIS-RTOS.
 */
osMailQId osMailCreate(const osMailQDef_t *queue_def, osThreadId thread_id)
{
#if defined(RT_USING_MEMPOOL) && defined(RT_USING_MAILBOX)
    (void)thread_id;
    os_mailQ_cb_t *ptr = RT_NULL;

    char name[RT_NAME_MAX];
    static rt_uint16_t mail_cnt = 0U;
    rt_size_t block_size;

    /* Cannot be called from Interrupt Service Routines. */
    if (inHandlerMode())
        goto mail_create_failed;

    if (RT_NULL == queue_def || 0 == queue_def->queue_sz || 0 == queue_def->item_sz)
        goto mail_create_failed;

    rt_snprintf(name, sizeof(name), "mb%02d", mail_cnt++);
    block_size = RT_ALIGN(queue_def->item_sz, RT_ALIGN_SIZE);

    ptr = (void *)CMSIS_MEM_ALLOC(sizeof(struct os_mailQ_cb));
    if (RT_NULL == ptr)
        goto mail_create_failed;

    ptr->mp_id = rt_mp_create(name, queue_def->queue_sz, block_size);
    ptr->mb_id = rt_mb_create(name, queue_def->queue_sz, RT_IPC_FLAG_PRIO);
    if ((RT_NULL == ptr->mp_id) || (RT_NULL == ptr->mb_id))
        goto mail_create_failed;

    return ptr;
mail_create_failed:
    if (ptr)
        CMSIS_MEM_FREE(ptr);
    if (RT_NULL != ptr->mp_id)
        rt_mp_delete(ptr->mp_id);
    if (RT_NULL != ptr->mb_id)
        rt_mb_delete(ptr->mb_id);
    LOG_E("CMSIS RTOS1 %s failed", __func__);
    return RT_NULL;
#else  /* RT_USING_MEMPOOL && RT_USING_MAILBOX */
    return RT_NULL;
#endif /* not define RT_USING_MEMPOOL && RT_USING_MAILBOX */
}

/**
 * @brief Allocate a memory block from a mail
 * @param  queue_id      mail queue ID obtained with \ref osMailCreate.
 * @param  millisec      timeout value or 0 in case of no time-out.
 * @retval pointer to memory block that can be filled with mail or NULL in case error.
 * @note   MUST REMAIN UNCHANGED: \b osMailAlloc shall be consistent in every CMSIS-RTOS.
 */
void *osMailAlloc(osMailQId queue_id, uint32_t millisec)
{
    os_mailQ_cb_t *ptr = (os_mailQ_cb_t *)queue_id;
    void *ret = NULL;
    rt_tick_t ticks = 0;

    if (RT_NULL == ptr)
        goto mail_alloc_error;

    if (osWaitForever == millisec)
        ticks = RT_WAITING_FOREVER;
    else if (0U != millisec)
        ticks = rt_tick_from_millisecond(millisec);

    ret = rt_mp_alloc(ptr->mp_id, ticks);
    if (RT_NULL == ret)
        goto mail_alloc_error;

    return ret;
mail_alloc_error:
    LOG_E("CMSIS RTOS1 %s failed", __func__);
    return RT_NULL;
}

/**
 * @brief Allocate a memory block from a mail and set memory block to zero
 * @param  queue_id      mail queue ID obtained with \ref osMailCreate.
 * @param  millisec      timeout value or 0 in case of no time-out.
 * @retval pointer to memory block that can be filled with mail or NULL in case error.
 * @note   MUST REMAIN UNCHANGED: \b osMailCAlloc shall be consistent in every CMSIS-RTOS.
 */
void *osMailCAlloc(osMailQId queue_id, uint32_t millisec)
{
    void *p = osMailAlloc(queue_id, millisec);
    if (p)
        rt_memset(p, 0, ((os_mailQ_cb_t *)queue_id)->mp_id->block_size);

    return p;
}

/**
 * @brief Put a mail to a queue
 * @param  queue_id      mail queue ID obtained with \ref osMailCreate.
 * @param  mail          memory block previously allocated with \ref osMailAlloc or \ref osMailCAlloc.
 * @retval status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osMailPut shall be consistent in every CMSIS-RTOS.
 */
osStatus osMailPut(osMailQId queue_id, void *mail)
{
    osStatus status;
    os_mailQ_cb_t *ptr = (os_mailQ_cb_t *)queue_id;
    rt_err_t result;

    if (RT_NULL == ptr)
    {
        status = osErrorParameter;
        goto mail_put_error;
    }

    if (RT_NULL == mail)
    {
        status = osErrorValue;
        goto mail_put_error;
    }

    result = rt_mb_send((ptr->mb_id), (rt_ubase_t)mail);
    if (RT_EOK != result)
    {
        status = osErrorOS;
        goto mail_put_error;
    }

    return osOK;
mail_put_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}

/**
 * @brief Get a mail from a queue
 * @param  queue_id   mail queue ID obtained with \ref osMailCreate.
 * @param millisec    timeout value or 0 in case of no time-out
 * @retval event that contains mail information or error code.
 * @note   MUST REMAIN UNCHANGED: \b osMailGet shall be consistent in every CMSIS-RTOS.
 */
osEvent osMailGet(osMailQId queue_id, uint32_t millisec)
{
    osEvent event;
    os_mailQ_cb_t *ptr = (os_mailQ_cb_t *)queue_id;
    rt_err_t result;
    rt_ubase_t value;
    rt_tick_t ticks = 0;

    if (RT_NULL == ptr)
    {
        event.status = osErrorParameter;
        goto mail_get_error;
    }

    if (osWaitForever == millisec)
        ticks = RT_WAITING_FOREVER;
    else if (0U != millisec)
        ticks = rt_tick_from_millisecond(millisec);

    result = rt_mb_recv((ptr->mb_id), &value, ticks);
    if (-RT_ETIMEOUT == result)
    {
        event.status = osEventTimeout;
        goto mail_get_error;
    }
    else if (RT_EOK != result)
    {
        event.status = osErrorOS;
        goto mail_get_error;
    }

    event.value.p = (void *)value;
    event.status = osEventMail;
    return event;
mail_get_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, event.status);
    return event;
}

/**
 * @brief Free a memory block from a mail
 * @param  queue_id mail queue ID obtained with \ref osMailCreate.
 * @param  mail     pointer to the memory block that was obtained with \ref osMailGet.
 * @retval status code that indicates the execution status of the function.
 * @note   MUST REMAIN UNCHANGED: \b osMailFree shall be consistent in every CMSIS-RTOS.
 */
osStatus osMailFree(osMailQId queue_id, void *mail)
{
    osStatus status;
    os_mailQ_cb_t *ptr = (os_mailQ_cb_t *)queue_id;

    if (RT_NULL == ptr)
    {
        status = osErrorParameter;
        goto mail_free_error;
    }

    if (RT_NULL == mail)
    {
        status = osErrorValue;
        goto mail_free_error;
    }

    rt_mp_free(mail);
    mail = RT_NULL;

    return osOK;
mail_free_error:
    LOG_E("CMSIS RTOS1 %s failed error code is : 0x%x", __func__, status);
    return status;
}
#endif /* Use Mail Queues */

/****************************** end of file ******************************/
