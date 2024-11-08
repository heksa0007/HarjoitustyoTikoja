#include <stdio.h>
#include <string.h>
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/PIN.h>
#include "Board.h"

#define STACKSIZE 2048
#define BUFFER_SIZE 128  // Määritellään puskuriin mahtuvien merkkien maksimimäärä
Char uartTaskStack[STACKSIZE];
Char logTaskStack[STACKSIZE];

// UART- ja I2C-kahvat
UART_Handle uart;
I2C_Handle i2c;

// LED-pinnin kahva
static PIN_Handle ledPinHandle;
static PIN_State ledPinState;

#define I2C_ADDRESS 0x48  // Aseta tämä oikeaan I2C-laitteen osoitteeseen

// Julistetaan sendToI2C-funktio
void sendToI2C(char character);

// LEDin konfiguraatio
PIN_Config ledPinTable[] = {
    Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

// Puskuri UART-merkkejä varten
char uartBuffer[BUFFER_SIZE];
int bufferIndex = 0;  // Seuraa seuraavaa vapaata paikkaa puskurissa

// Funktio, joka sytyttää LEDin lyhyeksi tai pitkäksi ajaksi
void flashLED(UArg arg0, UArg arg1, int duration) {
    PIN_setOutputValue(ledPinHandle, Board_LED1, 1); // Sytytä LED
    Task_sleep(duration); // Viive millisekunteina
    PIN_setOutputValue(ledPinHandle, Board_LED1, 0); // Sammuta LED
}

// Funktio, joka lähettää merkin I2C:n kautta
void sendToI2C(char character) {
    I2C_Transaction i2cTransaction;
    char message[2] = {character, '\0'};  // Luo viesti, joka sisältää merkin ja NULL-terminaattorin

    // I2C-lähetysasetukset
    i2cTransaction.slaveAddress = I2C_ADDRESS;
    i2cTransaction.writeBuf = message;
    i2cTransaction.writeCount = strlen(message);
    i2cTransaction.readCount = 0;

    // Lähetetään merkki I2C-laitteelle
    if (I2C_transfer(i2c, &i2cTransaction)) {
        System_printf("Character '%c' sent to I2C\n", character);
    } else {
        System_printf("Error: Failed to send character '%c' to I2C\n", character);
    }
    System_flush();
}

// UART-lukufunktio ja merkkien tallennus puskuriin
Void uartTaskFxn(UArg arg0, UArg arg1) {
    UART_Params uartParams;
    char receivedChar;

    // UART-parametrien alustaminen
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

    // Lue UARTista merkkejä ja tallenna ne puskuriin
    while (1) {
        UART_read(uart, &receivedChar, 1);  // Lue yksi merkki UARTin kautta

        // Tallenna merkki puskuriin, jos tilaa on
        if (bufferIndex < BUFFER_SIZE - 1) {
            uartBuffer[bufferIndex++] = receivedChar;
            uartBuffer[bufferIndex] = '\0';  // Päivitetään nollaterminaattori
        } else {
            System_printf("Buffer full, character discarded: '%c'\n", receivedChar);
            System_flush();
        }

        // Sytytä LED eri pituiseksi ajaksi riippuen merkistä
        if (receivedChar == '.') {
            flashLED(arg0, arg1, 500);  // Lyhyt välähdys (50 ms)
        } else if (receivedChar == '-') {
            flashLED(arg0, arg1, 1500);  // Pitkä välähdys (150 ms)
        } else if (receivedChar == ' ') {
            Task_sleep(1000); // 1 sekunnin tauko, jos vastaanotetaan välilyönti
        }

        // Puolen sekunnin tauko jokaisen merkin jälkeen
        Task_sleep(500);
    }
}

// Tehtävä, joka lukee puskuriin tallennetut merkit ja tyhjentää sen 10 sekunnin välein
Void logTaskFxn(UArg arg0, UArg arg1) {
    while (1) {
        Task_sleep(100000);  // Odota 10 sekuntia

        // Tulostetaan puskuri ja nollataan se
        System_printf("Buffer contents: %s\n", uartBuffer);
        System_flush();

        // Nollataan puskuri
        memset(uartBuffer, 0, sizeof(uartBuffer));
        bufferIndex = 0;  // Nollaa indeksi
    }
}

/* Main-funktio */
Int main(void) {
    Task_Handle uartTaskHandle, logTaskHandle;
    Task_Params uartTaskParams, logTaskParams;

    Board_initGeneral();
    Board_initUART();
    Board_initI2C();

    // LEDin alustaminen
    ledPinHandle = PIN_open(&ledPinState, ledPinTable);
    if (!ledPinHandle) {
        System_abort("Error initializing LED pin\n");
    }

    // Luodaan UART-lukutehtävä
    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority = 2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    // Luodaan loggaustehtävä
    Task_Params_init(&logTaskParams);
    logTaskParams.stackSize = STACKSIZE;
    logTaskParams.stack = &logTaskStack;
    logTaskParams.priority = 1;
    logTaskHandle = Task_create(logTaskFxn, &logTaskParams, NULL);
    if (logTaskHandle == NULL) {
        System_abort("Log task create failed!");
    }

    // Käynnistetään BIOS
    BIOS_start();

    return (0);
}

