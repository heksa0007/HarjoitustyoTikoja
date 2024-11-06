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

//RTOSmuuttujat käyttöön
static PIN_Handle button0Handle;
static PIN_State button0State;
static PIN_Handle button1Handle;
static PIN_State button1State;
static PIN_Handle led0Handle;
static PIN_State led0State;
static PIN_Handle led1Handle;
static PIN_State led1State;


//Alustetaan pinniconfiguraatiot:

// Painonappi 0
PIN_Config button0Config[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};

// Painonappi 1 (on-off)
PIN_Config button2Config[] = {
   Board_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};

// Ledi 0
PIN_Config led0Config[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};

// Ledi 1
PIN_Config led1Config[] = {
   Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};


// JTKJ: Tehtävä 3. Tilakoneen esittely
enum state { WAITING=1, DATA_READY };
enum state programState = WAITING;

// JTKJ: Tehtävä 3. Valoisuuden globaali muuttuja
double ambientLight = -1000.0;

// Painonappien RTOS-muuttujat ja alustus

void button0Fxn(PIN_Handle handle, PIN_Id pinId) {
    System_printf("Button 0 pressed");
    // Vaihdetaan led-pinnin tilaa negaatiolla
    uint_t pinValue = PIN_getOutputValue( Board_LED0 );
    pinValue = !pinValue;
    PIN_setOutputValue( led0Handle, Board_LED0, pinValue );
}

void button1Fxn(PIN_Handle handle, PIN_Id pinId) {
    System_printf("Button 1 pressed");
    // Vaihdetaan led-pinnin tilaa negaatiolla
    uint_t pinValue = PIN_getOutputValue( Board_LED1 );
    pinValue = !pinValue;
    PIN_setOutputValue( led1Handle, Board_LED1, pinValue );
}

/* Task Functions */
Void uartTaskFxn(UArg arg0, UArg arg1) {

    // JTKJ: Tehtävä 4. Lisää UARTin alustus: 9600,8n1

    UART_Handle uart;
    UART_Params uartParams;
    char echoMsg[5];

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

        // JTKJ: Tehtävä 3. Kun tila on oikea, tulosta sensoridata merkkijonossa debug-ikkunaan
        //       Muista tilamuutos
        // JTKJ: Exercise 3. Print out sensor data as string to debug window if the state is correct
        //       Remember to modify state

        if (programState == DATA_READY) {
            char optDataStr[5];
            snprintf(optDataStr, 5, "%f\n", ambientLight);
            System_printf(optDataStr);

            // UART write:
            sprintf(echoMsg, "%f\n\r", ambientLight);
            UART_write(uart, echoMsg, strlen(echoMsg));

            programState = WAITING;
        }

        // JTKJ: Tehtävä 4. Lähetä sama merkkijono UARTilla

        // Just for sanity check for exercise, you can comment this out
        System_printf("uartTask\n");
        System_flush();

        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Void sensorTaskFxn(UArg arg0, UArg arg1) {

    I2C_Handle      i2c;
    I2C_Params      i2cParams;

    // JTKJ: Tehtävä 2. Avaa i2c-väylä taskin kyttöön

    // Alustetaan i2c-väylä
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;

    // Avataan I2C väylä
    i2c = I2C_open(Board_I2C_TMP, &i2cParams);
    if (i2c == NULL) {
        System_abort("Error Initializing I2C\n");
    }

    // JTKJ: Tehtävä 2. Alusta sensorin OPT3001 setup-funktiolla
    //       Laita ennen funktiokutsua eteen 100ms viive (Task_sleep)
    Task_sleep(10000 / Clock_tickPeriod);
    opt3001_setup(&i2c);

    while (1) {

        // JTKJ: Tehtävä 2. Lue sensorilta dataa ja tulosta se Debug-ikkunaan merkkijonona

        double optData = opt3001_get_data(&i2c);
        /*char optDataStr[5];
        snprintf(optDataStr, 5, "%f", optData);
        System_printf(optDataStr);*/

        // JTKJ: Tehtävä 3. Tallenna mittausarvo globaaliin muuttujaan
        //       Muista tilamuutos

        ambientLight = optData;
        programState = DATA_READY;

        // Just for sanity check for exercise, you can comment this out
        System_printf("sensorTask\n");
        System_flush();

        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Int main(void) {

    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;

    // Initialize board
    Board_initGeneral();


    // Väylä mukaan ohjelmaan
    Board_initI2C();

    // UART käyttöön ohjelmassa
    Board_initUART();


    // Ledi käyttöön ohjelmassa
    led0Handle = PIN_open( &led0State, led0Config );
    if(!led0Handle) {
       System_abort("Error initializing LED pin\n");
    }
    led1Handle = PIN_open( &led1State, led1Config );
    if(!led1Handle) {
       System_abort("Error initializing LED pin\n");
    }

    // TODO: Toinen ledi käyttöön ohjelmassa
    // Toinen ledi käyttöön


    // Painonappi 0 käyttöön ohjelmassa
    button0Handle = PIN_open(&button0State, button0Config);
    if(!button0Handle) {
       System_abort("Error initializing button0 pin\n");
    }

    // Painonappi 1 käyttöön ohjelmassa
    button1Handle = PIN_open(&button1State, button1Config);
    if(!button1Handle) {
       System_abort("Error initializing button1 pin\n");
    }

    // Painonapille 0 keskeytyksen käsittellijä
    if (PIN_registerIntCb(button0Handle, &button0Fxn) != 0) {
       System_abort("Error registering button callback function");
    }

    // Painonapille 1 keskeytyksen käsittellijä
    if (PIN_registerIntCb(button1Handle, &button1Fxn) != 0) {
       System_abort("Error registering button callback function");
    }

    /* Task */
    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority=2;
    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority=2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    /* Sanity check */
    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
