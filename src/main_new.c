/*
 * This file is part of the µOS++ distribution.
 *   (https://github.com/micro-os-plus)
 * Copyright (c) 2014 Liviu Ionescu.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

// ----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include "diag/trace.h"

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"

#include "list.h"
#include "croutine.h"
//#include "FreeRTOSConfig.h"
#include "portable.h"

#define CCM_RAM __attribute__((section(".ccmram")))

// ----------------------------------------------------------------------------

#include "led.h"

#define BLINK_PORT_NUMBER         (3)
#define BLINK_PIN_NUMBER_GREEN    (12)
#define BLINK_PIN_NUMBER_ORANGE   (13)
#define BLINK_PIN_NUMBER_RED      (14)
#define BLINK_PIN_NUMBER_BLUE     (15)
#define BLINK_ACTIVE_LOW          (false)

struct led blinkLeds[4];

// ----------------------------------------------------------------------------
/*-----------------------------------------------------------*/

/*
 * The LED timer callback function.  This does nothing but switch the red LED
 * off.
 */

static void SenderTimerCallback( TimerHandle_t xTimer );
static void ReceiverTimerCallback( TimerHandle_t xTimer );
void xSenderTask( void * pvParameters );
void xReceiverTask( void * pvParameters );
void Init(void);
/*-----------------------------------------------------------*/

/* The LED software timer.  This uses vLEDTimerCallback() as its callback
 * function.
 */

static TaskHandle_t xHandleSender = NULL;
static TaskHandle_t xHandleReceiver = NULL;
static TimerHandle_t xTimerSender = NULL;
static TimerHandle_t xTimerReceiver = NULL;
static SemaphoreHandle_t xSemaphoreSender=NULL;
static SemaphoreHandle_t xSemaphoreReceiver=NULL;
static QueueHandle_t xQueueSender=NULL;

//counters
unsigned int blocked_send,success_send,success_receive;


//validity checkers
BaseType_t xReturnedSender;
BaseType_t xReturnedReceiver;
BaseType_t xReturnedQueue;
BaseType_t xTimerSenderStarted, xTimerReceiverStarted;

//array of sender periods
unsigned int arr[]={100, 140,180, 220, 260, 300};

//struct that holds the data sent to the queue
struct Time{
	char str[10];
	TickType_t ticks;  //unsigned int
};

/*-----------------------------------------------------------*/
// ----------------------------------------------------------------------------
//
// Semihosting STM32F4 empty sample (trace via DEBUG).
//
// Trace support is enabled by adding the TRACE macro definition.
// By default the trace messages are forwarded to the DEBUG output,
// but can be rerouted to any device or completely suppressed, by
// changing the definitions required in system/src/diag/trace-impl.c
// (currently OS_USE_TRACE_ITM, OS_USE_TRACE_SEMIHOSTING_DEBUG/_STDOUT).
//

// ----- main() ---------------------------------------------------------------

// Sample pragmas to cope with warnings. Please note the related line at
// the end of this function, used to pop the compiler diagnostics status.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wreturn-type"


int
main(int argc, char* argv[])
{

	//timers creation
	xTimerReceiver = xTimerCreate( "TimerReceiver", ( pdMS_TO_TICKS(200) ), pdTRUE,0, ReceiverTimerCallback);
	xTimerSender = xTimerCreate( "TimerSender", ( pdMS_TO_TICKS(100) ), pdTRUE,0, SenderTimerCallback);

	//semaphore creation
	xSemaphoreSender= xSemaphoreCreateBinary();
	xSemaphoreReceiver= xSemaphoreCreateBinary();
	//Queue of pointers to a struct
	xQueueSender=xQueueCreate(2, sizeof(struct Time *));
	//task creation
	xReturnedSender=xTaskCreate(xSenderTask,"sender",1024,0,1,&xHandleSender);
	xReturnedReceiver=xTaskCreate(xReceiverTask,"receiver",1024,0,2,&xHandleReceiver);
	//Init function.
	Init();

	//timer create validation
	if( ( xTimerSender != NULL ) && ( xTimerReceiver != NULL ) )
	{
		xTimerReceiverStarted = xTimerStart( xTimerReceiver, 0 );
		xTimerSenderStarted = xTimerStart( xTimerSender, 0 );
	}
	//timer start validation
	if( xTimerSenderStarted == pdPASS && xTimerReceiverStarted == pdPASS)
	{
		//task create validation
		if(xReturnedSender == pdPASS && xReturnedReceiver == pdPASS)
		vTaskStartScheduler();
	}

	return 0;
}

#pragma GCC diagnostic pop

// ----------------------------------------------------------------------------
void Init(void)
{
	static unsigned char count = 0;
	if(count == 0){
		blocked_send = 0;
		success_send = 0;
		success_receive = 0;
	}
	else{
		trace_printf("Total successfully sent: %d\n", success_send);
		trace_printf("Total blocked from sending: %d\n", blocked_send);
		trace_printf("Total successfully received: %d\n", success_receive);
		blocked_send = 0;
		success_send = 0;
		success_receive = 0;
		xQueueReset( xQueueSender );

	}
	if(count == 6){
		xTimerDelete( xTimerSender, ( TickType_t ) 10);
		xTimerDelete( xTimerReceiver, ( TickType_t ) 10);
		trace_puts("Game Over!!");
		vTaskEndScheduler();
	}

	xTimerChangePeriod(xTimerSender,arr[count]/portTICK_PERIOD_MS,portMAX_DELAY);
	count++;


}
void xSenderTask( void * pvParameters )
{
	//initialize the struct with the default string
	struct Time senderTime = {"Time is "};
	struct Time* senderTimePtr = &senderTime;	//pointer to the struct
	while(1)
	{

		if(xSemaphoreTake( xSemaphoreSender,portMAX_DELAY)==pdTRUE)
		{
			senderTime.ticks=xTaskGetTickCount();
			//trace_puts("hi in sender task"); //trace_printf causes a hang in the program
			xReturnedQueue=xQueueSend( xQueueSender,( void * ) &senderTimePtr,0);
			if(xReturnedQueue == pdTRUE)
			{
				success_send++;
			}
			else
			{
				blocked_send++;
			}
		}
	}
}
void xReceiverTask( void * pvParameters )
{
	struct Time receiverTime;
	struct Time* receiverTimePtr;
	while(1)
	{

		if(xSemaphoreTake( xSemaphoreReceiver,portMAX_DELAY)==pdTRUE)
		{
			trace_puts("hi in receiver task");

			xReturnedQueue=xQueueReceive(xQueueSender, (void *) &receiverTimePtr,0);
			if(xReturnedQueue==pdTRUE)
			{
				success_receive++;
			}
			if(success_receive==500)
			{
				Init();
			}
		}}
}
static void SenderTimerCallback( TimerHandle_t xTimer )
{
	xSemaphoreGive(xSemaphoreSender);
	//trace_printf("hi in sender callback\n");
}

static void ReceiverTimerCallback( TimerHandle_t xTimer )
{
	xSemaphoreGive(xSemaphoreReceiver);
	//trace_puts("hi in receiver callback");
}


void vApplicationMallocFailedHook( void )
{
	/* Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
volatile size_t xFreeStackSpace;

	/* This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amout of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}

void vApplicationTickHook(void) {
}

StaticTask_t xIdleTaskTCB CCM_RAM;
StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] CCM_RAM;

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
  /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
  state will be stored. */
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

  /* Pass out the array that will be used as the Idle task's stack. */
  *ppxIdleTaskStackBuffer = uxIdleTaskStack;

  /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
  Note that, as the array is necessarily of type StackType_t,
  configMINIMAL_STACK_SIZE is specified in words, not bytes. */
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t xTimerTaskTCB CCM_RAM;
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH] CCM_RAM;

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
  *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
  *ppxTimerTaskStackBuffer = uxTimerTaskStack;
  *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

