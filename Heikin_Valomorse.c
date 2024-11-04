/* C Standard library */
#include <stdio.h>
#include <string.h>

/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/drivers/UART.h>

/* Board Header files */
#include "Board.h"
#include "sensors/opt3001.h"

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];

// RTOS muuttujat käyttöön
static PIN_Handle buttonHandle;
static PIN_State buttonState;
static PIN_Handle ledHandle;
static PIN_State ledState;

// Alustetaan pinni konfiguraatiot (Vaihda nappi toiseen, esim. Board_BUTTON1)
PIN_Config buttonConfig[] = {
   Board_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,  // Käytä Board_BUTTON1
   PIN_TERMINATE
};

PIN_Config ledConfig[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};

// Tilakoneen tila
enum state { WAITING_01=1, WAITING_OVER1 };
enum state programState = WAITING_01;

// Valoisuuden globaali muuttuja
double ambientLight = -1000.0;
double previousAmbientLight = -1000.0;  // Tallennetaan edellinen mittausarvo

// UART-kahva
UART_Handle uart;

// Poistettu napin toiminnallisuus, napin painallus ei tee mitään
void buttonFxn(PIN_Handle handle, PIN_Id pinId) {
    // Ei tehdä mitään napin painalluksessa
}

/* Task Functions */
Void uartTaskFxn(UArg arg0, UArg arg1) {
    UART_Params uartParams;

    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.readMode = UART_MODE_BLOCKING;
    uartParams.baudRate = 9600; // nopeus 9600baud
    uartParams.dataLength = UART_LEN_8; // 8
    uartParams.parityType = UART_PAR_NONE; // n
    uartParams.stopBits = UART_STOP_ONE; // 1

    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL) {
        System_abort("Error opening the UART");
    }

    while (1) {
        // Tarkistetaan, onko anturin arvo välillä 0 ja 1, ja onko edellinen arvo ollut yli 1
        if (ambientLight >= 0.0 && ambientLight <= 1.0 && previousAmbientLight > 1.0) {
            const char point[] = ".";
            UART_write(uart, point, strlen(point));
        }
        // Tarkistetaan, onko anturin arvo yli 2000, ja onko edellinen arvo ollut alle 2000
        else if (ambientLight > 2000.0 && previousAmbientLight <= 2000.0) {
            const char newline[] = "\r\n";
            UART_write(uart, newline, strlen(newline));
        }

        // Päivitetään edellinen mittausarvo
        previousAmbientLight = ambientLight;

        Task_sleep(1000000 / Clock_tickPeriod);  // Sleep 1 second
    }
}

Void sensorTaskFxn(UArg arg0, UArg arg1) {
    I2C_Handle i2c;
    I2C_Params i2cParams;

    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;

    i2c = I2C_open(Board_I2C_TMP, &i2cParams);
    if (i2c == NULL) {
        System_abort("Error Initializing I2C\n");
    }

    Task_sleep(10000 / Clock_tickPeriod);
    opt3001_setup(&i2c);

    while (1) {
        // Lue anturidataa
        double optData = opt3001_get_data(&i2c);
        ambientLight = optData;

        Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Int main(void) {
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;

    // Alustetaan lauta
    Board_initGeneral();
    Board_initI2C();
    Board_initUART();

    // Alustetaan LED-pinni
    ledHandle = PIN_open(&ledState, ledConfig);
    if (!ledHandle) {
        System_abort("Error initializing LED pin\n");
    }

    // Alustetaan napin pinni (nyt käytetään Board_BUTTON1)
    buttonHandle = PIN_open(&buttonState, buttonConfig);
    if (!buttonHandle) {
        System_abort("Error initializing button pin\n");
    }

    // Rekisteröidään napin keskeytyksen käsittelijä (nyt se ei tee mitään)
    if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0) {
        System_abort("Error registering button callback function");
    }

    // Luodaan sensorin tehtävä
    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority = 2;
    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    // Luodaan UARTin tehtävä
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


