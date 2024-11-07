#include <stdio.h>
#include <string.h>
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/UART.h>
#include "Board.h"

#define STACKSIZE 2048
Char uartTaskStack[STACKSIZE];

// UART- ja I2C-kahvat
UART_Handle uart;
I2C_Handle i2c;

#define I2C_ADDRESS 0x48  // Aseta tämä oikeaan I2C-laitteen osoitteeseen

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

        // Virheenkorjauksen tulosteet
        System_printf("I2C transaction details:\n");
        System_printf(" - Slave address: 0x%X\n", i2cTransaction.slaveAddress);
        System_printf(" - Message: %s\n", message);
        System_printf(" - Message length: %d\n", (int)strlen(message));
        System_flush();
    }
    System_flush();
}

// UART-lukufunktio
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

    // I2C-parametrien alustaminen
    I2C_Params i2cParams;
    I2C_Params_init(&i2cParams);
    i2c = I2C_open(Board_I2C, &i2cParams);
    if (i2c == NULL) {
        System_abort("Error initializing I2C\n");
    }

    System_printf("Waiting for UART input...\n");
    System_flush();

    // Lue UARTista merkkejä ja lähetä ne suoraan I2C:lle
    while (1) {
        UART_read(uart, &receivedChar, 1);  // Lue yksi merkki UARTin kautta
        sendToI2C(receivedChar);            // Lähetä merkki sellaisenaan I2C:n kautta
    }
}

/* Main-funktio */
Int main(void) {
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;

    Board_initGeneral();
    Board_initUART();
    Board_initI2C();

    // Luodaan UART-lukutehtävä
    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority = 2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    // Käynnistetään BIOS
    BIOS_start();

    return (0);
}

