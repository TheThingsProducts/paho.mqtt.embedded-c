/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Allan Stockdill-Mander - initial API and implementation and/or initial documentation
 *    Ian Craggs - convert to FreeRTOS
 *    Johan Stokking - convert to Microchip Harmony
 *******************************************************************************/

#include "MQTTHarmony.h"

void MutexInit(Mutex *mutex) {
    mutex->sem = xSemaphoreCreateMutex();
}

int MutexLock(Mutex *mutex) {
    return xSemaphoreTake(mutex->sem, portMAX_DELAY);
}

int MutexUnlock(Mutex *mutex) {
    return xSemaphoreGive(mutex->sem);
}

int MutexDestroy(Mutex *mutex) {
    vSemaphoreDelete(mutex->sem);
    return 0;
}

void TimerCountdownMS(Timer *timer, unsigned int timeout_ms) {
    timer->xTicksToWait = timeout_ms / portTICK_PERIOD_MS; /* convert milliseconds to ticks */
    vTaskSetTimeOutState(&timer->xTimeOut); /* Record the time at which this function was entered. */
}

void TimerCountdown(Timer *timer, unsigned int timeout) {
    TimerCountdownMS(timer, timeout * 1000);
}

int TimerLeftMS(Timer *timer) {
    xTaskCheckForTimeOut(&timer->xTimeOut, &timer->xTicksToWait); /* updates xTicksToWait to the number left */
    return (timer->xTicksToWait < 0) ? 0 : (timer->xTicksToWait * portTICK_PERIOD_MS);
}

char TimerIsExpired(Timer *timer) {
    return xTaskCheckForTimeOut(&timer->xTimeOut, &timer->xTicksToWait) == pdTRUE;
}

void TimerInit(Timer *timer) {
    timer->xTicksToWait = 0;
    timer->xTimeOut.xOverflowCount = 0;
    timer->xTimeOut.xTimeOnEntering = 0;
   // memset(&timer->xTimeOut->xOverflowCount, '\0', sizeof (timer->xTimeOut));
}

int Harmony_read(Network *n, unsigned char *buffer, int len, int timeout_ms) {
    TickType_t xTicksToWait = timeout_ms / portTICK_PERIOD_MS; /* convert milliseconds to ticks */
    TimeOut_t xTimeOut;
    int recvLen = 0;

    vTaskSetTimeOutState(&xTimeOut); /* Record the time at which this function was entered. */
    do {
        int rc = NET_PRES_SocketRead(n->my_socket, buffer + recvLen, len - recvLen);
        if (rc > 0)
            recvLen += rc;
        else if (rc == 0) {
          //  SYS_DEBUG(SYS_ERROR_ERROR, "RF");
            recvLen = 0;
         //   break;
        }
        else if (rc < 0) {
            recvLen = rc;
            break;
        }
    } while (recvLen < len && xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == pdFALSE);

    if(xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait)==pdTRUE) return -3;
    return recvLen;
}

int Harmony_write(Network *n, unsigned char *buffer, int len, int timeout_ms) {
    TickType_t xTicksToWait = timeout_ms / portTICK_PERIOD_MS; /* convert milliseconds to ticks */
    TimeOut_t xTimeOut;
    int sentLen = 0;

    vTaskSetTimeOutState(&xTimeOut); /* Record the time at which this function was entered. */
    do {
        int rc = NET_PRES_SocketWrite(n->my_socket, buffer + sentLen, len - sentLen);
        if (rc > 0)
            sentLen += rc;
        else if (rc == 0) {
            sentLen = 0;
            break;
        } else if (rc < 0) {
            sentLen = rc;
            break;
        }
    } while (sentLen < len && xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == pdFALSE);

    return sentLen;
}

void NetworkInit(Network *n) {
    n->my_socket = INVALID_SOCKET;
    n->mqttread = &Harmony_read;
    n->mqttwrite = &Harmony_write;
}

int NetworkConnect(Network *n, char *addr, int port) {
    IP_MULTI_ADDRESS remoteAddress;
    if (strlen(addr) == 0)
        return -1;

    if (!TCPIP_Helper_StringToIPAddress(addr, &remoteAddress.v4Add))
        return -1;

    SYS_PRINT("Connecting to: %s\r\n",addr);
    n->my_socket = NET_PRES_SocketOpen(0,
            NET_PRES_SKT_UNENCRYPTED_STREAM_CLIENT,
            IP_ADDRESS_TYPE_IPV4,
            port, 
            (NET_PRES_ADDRESS *)&remoteAddress,
            NULL);
    if (n->my_socket == INVALID_SOCKET)
        return INVALID_SOCKET;
    return 0;
}

void NetworkDisconnect(Network *n) {
    if (n->my_socket != INVALID_SOCKET) {
        NET_PRES_SocketDisconnect(n->my_socket);
        NET_PRES_SocketClose(n->my_socket);
        n->my_socket = INVALID_SOCKET;
    }
}

int ThreadStart(Thread *thread, void (*fn)(void *), void *arg) {
    uint16_t usTaskStackSize = 4096;
    UBaseType_t uxTaskPriority = 1; // Give lowest prio (busy whyle loop) FIXME: centralize prio config uxTaskPriorityGet(NULL); /* set the priority as the same as the calling task*/

    return xTaskCreate(fn, /* The function that implements the task. */
            "MQTTTask", /* Just a text name for the task to aid debugging. */
            usTaskStackSize, /* The stack size is defined in FreeRTOSIPConfig.h. */
            arg, /* The task parameter, not used in this case. */
            uxTaskPriority, /* The priority assigned to the task is defined in FreeRTOSConfig.h. */
            &thread->task); /* The task handle is used for killing. */
}

int ThreadJoin(Thread *thread) {
    if (thread->task != NULL) {
        vTaskDelete(thread->task);
        thread->task = NULL;
    }
    return SUCCESS;
}

void ThreadExit() {
}

void QueueInit(Queue *q) {
    q->queue = xQueueCreate(1, sizeof (unsigned short));
}

int Enqueue(Queue *q, unsigned short item) {
    return xQueueSend(q->queue, &item, 0U) == pdTRUE ? SUCCESS : FAILURE;
}

int Dequeue(Queue *q, unsigned short *item, Timer *timer) {
    xTaskCheckForTimeOut(&timer->xTimeOut, &timer->xTicksToWait);
    return xQueueReceive(q->queue, item, timer->xTicksToWait) == pdTRUE ? SUCCESS : TIMEOUT;
}

int QueueDestroy(Queue *q) {
    if (q->queue != NULL) {
        vQueueDelete(q->queue);
        q->queue = NULL;
    }
    return SUCCESS;
}
