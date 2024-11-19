#include <stdio.h>
#include <string.h>
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/PIN.h>
#include "Board.h"

#define STACKSIZE 2048
#define BUFFER_SIZE 128

Char uartTaskStack[STACKSIZE];
Char monitorTaskStack[STACKSIZE];
Char logTaskStack[STACKSIZE];

int pointState1 = 0, pointState = 0, lineState = 0, memoryState1 = 0, memoryState2 = 0;
UInt32 lastMessageTime = 0;

// UART- ja I2C-kahvat
UART_Handle uart;
static PIN_Handle ledPinHandle;
static PIN_State ledPinState;

PIN_Config ledPinTable[] = {
    Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

void flashLED(int duration) {
    PIN_setOutputValue(ledPinHandle, Board_LED1, 1);
    Task_sleep(duration);
    PIN_setOutputValue(ledPinHandle, Board_LED1, 0);
}

// Funktio, joka tarkistaa viestien vastaanottoajan ja nollaa pointStaten, jos viestiä ei tule sekunnin sisällä
Void monitorTaskFxn(UArg arg0, UArg arg1) {
    while (1) {
        UInt32 currentTime = Clock_getTicks();
        if ((currentTime - lastMessageTime) > (3000000 / Clock_tickPeriod)) {
            pointState = 0;
            lineState = 0;
        }
        Task_sleep(500);  // Tarkista 0,5 sekunnin välein
    }
}

// UART-lukufunktio ja merkkien käsittely
Void uartTaskFxn(UArg arg0, UArg arg1) {
    UART_Params uartParams;
    char receivedChar;

    UART_Params_init(&uartParams);
    uartParams.readMode = UART_MODE_BLOCKING;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_ON;
    uartParams.baudRate = 9600;

    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL) {
        System_abort("Error opening the UART");
    }

    System_printf("Waiting for UART input...\n");
    System_flush();

    while (1) {
        UART_read(uart, &receivedChar, 1);

        lastMessageTime = Clock_getTicks();  // Päivitetään viimeisimmän viestin aika

        if (receivedChar == '.') {
            flashLED(500);
            pointState++;
            System_printf("Received: '.', pointState: %d\n", pointState); // Tulostus tarkistamiseen
        } else if (receivedChar == '-') {
            flashLED(1000);
            lineState++;
            System_printf("Received: '-', pointState: %d\n", pointState); // Tulostus tarkistamiseen
        } else if (receivedChar == ' ') {
            Task_sleep(1000);

            System_printf("Space received. Current pointState: %d\n", pointState); // Lisätty tarkistukseen


        }

        Task_sleep(500);
    }
}

// Funktio, joka tulostaa pointState-arvon sekunnin välein
Void logTaskFxn(UArg arg0, UArg arg1) {
    while (1) {
        System_printf("pointState: %d\n", pointState);
        System_flush();
        if (pointState == 6 && lineState == 3) {
                        System_printf("SOS detected!\n");
                        System_flush();

                        int i;
                        for (i = 0; i < 3; i++) {
                            flashLED(200);
                            Task_sleep(200);
                        }


                        pointState = 0; // Nollataan pointState
                    }
        Task_sleep(1000000 / Clock_tickPeriod);  // 1 sekunnin viive
    }
}

/* Main function */
Int main(void) {
    Task_Handle uartTaskHandle, monitorTaskHandle, logTaskHandle;
    Task_Params uartTaskParams, monitorTaskParams, logTaskParams;

    Board_initGeneral();
    Board_initUART();

    ledPinHandle = PIN_open(&ledPinState, ledPinTable);
    if (!ledPinHandle) {
        System_abort("Error initializing LED pin\n");
    }

    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority = 2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("UART task create failed!");
    }

    Task_Params_init(&monitorTaskParams);
    monitorTaskParams.stackSize = STACKSIZE;
    monitorTaskParams.stack = &monitorTaskStack;
    monitorTaskParams.priority = 1;
    monitorTaskHandle = Task_create(monitorTaskFxn, &monitorTaskParams, NULL);
    if (monitorTaskHandle == NULL) {
        System_abort("Monitor task create failed!");
    }

    Task_Params_init(&logTaskParams);
    logTaskParams.stackSize = STACKSIZE;
    logTaskParams.stack = &logTaskStack;
    logTaskParams.priority = 1;
    logTaskHandle = Task_create(logTaskFxn, &logTaskParams, NULL);
    if (logTaskHandle == NULL) {
        System_abort("Log task create failed!");
    }

    BIOS_start();
    return 0;
}

