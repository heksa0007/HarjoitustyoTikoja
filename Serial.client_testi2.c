#include <stdio.h>
#include <string.h>
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/PIN.h>
#include <ti/sysbios/knl/Clock.h>
#include "Board.h"

#define STACKSIZE 2048
#define BUFFER_SIZE 128

Char uartTaskStack[STACKSIZE];
Char logTaskStack[STACKSIZE];

int pointState1 = 0, pointState = 0, lineState = 0, memoryState1 = 0, memoryState2 = 0;

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

        if (receivedChar == '.') {
            flashLED(500);
                pointState++;

        } else if (receivedChar == '-') {
            flashLED(1000);
            lineState++;
        } else if (receivedChar == ' ') {
            Task_sleep(1000);



            if (pointState == 6 && lineState == 3) {
                System_printf("SOS detected!\n");
                System_flush();

                int i;
                for (i = 0; i < 3; i++) {
                    flashLED(200);
                    Task_sleep(200);
                }


                memoryState1 = 0;
            }
        }

        Task_sleep(500);
    }
}

Void logTaskFxn(UArg arg0, UArg arg1) {
    while (1) {
        System_printf("pS: %d, pS1: %d, lS: %d, mS1: %d, mS1: %d\n", pointState, pointState1, lineState, memoryState1, memoryState2);
        System_flush();
        Task_sleep(1000000 / Clock_tickPeriod);  // 1 sekunnin viive
    }
}

/* Main function */
Int main(void) {
    Task_Handle uartTaskHandle, logTaskHandle;
    Task_Params uartTaskParams, logTaskParams;

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
        System_abort("Task create failed!");
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
