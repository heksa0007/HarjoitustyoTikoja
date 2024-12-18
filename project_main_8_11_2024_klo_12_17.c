/* C Standard library */
#include <stdio.h>
#include <string.h>
#include <math.h>

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
#include <ti/drivers/i2c/I2CCC26XX.h>

/* Board Header files */
#include "Board.h"
#include "sensors/opt3001.h"
#include "sensors/mpu9250.h"


/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];




//// RTOS-muuttujat käyttöön:

// Napit
static PIN_Handle button0Handle;
static PIN_State button0State;
static PIN_Handle button1Handle;
static PIN_State button1State;
// Ledit
static PIN_Handle led0Handle;
static PIN_State led0State;
static PIN_Handle led1Handle;
static PIN_State led1State;
// MPU power pin global variables (kiihtyvyysanturi)
static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;



//// Alustetaan pinniconfiguraatiot:

// Painonappi 0 (toiminto)
PIN_Config button0Config[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};

// Painonappi 1 (on-off)
PIN_Config button1Config[] = {
   Board_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};

// Ledi 0 (vihreä)
PIN_Config led0Config[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};

// Ledi 1 (punainen)
PIN_Config led1Config[] = {
   Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};

// MPU power pin (kiihtyvyysanturi)
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};



// MPU uses its own I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};



// Tilakoneen esittely
enum state { WAITING=1, DATA_READY,
             READ_COMMANDS, READ_CHARACTERS,
             SEND_MESSAGE, MESSAGE_SENT,
             RECEIVING_MESSAGE, MESSAGE_RECEIVED, SHOW_MESSAGE};
enum state programState = WAITING;

// Alempi tilakone, johon tallennetaan viestin lähettämiseen liittyvä tila.
// Siksi, että se voidaan palauttaa viestin vastaanottamisen jälkeen
// ja voidaan jatkaa viestin kirjoittamista välittömästi uudelleen
enum state programStateWriting = WAITING;

// Tallennusmuuttuja vastaanotetuille viesteille
char receivedMessageBuffer[BUFFER_SIZE];



/*
// Valoisuuden globaali muuttuja
double ambientLight = -1000.0;
*/

// kiihtyvyyden ja gyroskooppien globaalit muuttujat (float-tyyppisenä)
float acl_x = 0.0;
float acl_y = 0.0;
float acl_z = 0.0;
float gyro_x = 0.0;
float gyro_y = 0.0;
float gyro_z = 0.0;


// UART- ja I2C-kahvat
UART_Handle uart;
I2C_Handle i2cMPU;

// MPU I2C configuration
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};



// Funktio yksittäisen symbolin lähettämiseen UARTin kautta
void sendToUART(const char* symbol) {
    char message[5];
    sprintf(message, "%s\r\n\0", symbol);  // Muodostaa viestin, joka sisältää 4 tavua
    UART_write(uart, message, strlen(message) + 1);  // Lähetetään 4 tavua (mukaan lukien \0)
    Task_sleep(100000 / Clock_tickPeriod);  // Pieni viive viestien välillä
}



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

    // UARTin alustus: 9600,8n1

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


        //// TILAKONE

        if (programState == DATA_READY) {

            // Kun saadaan sensoridataa: mitä tehdään? Tarvitaanko tätä ollenkaan?

            // Valosensori:
            /*
            char optDataStr[5];
            snprintf(optDataStr, 5, "%f\n", ambientLight);
            System_printf(optDataStr);

            // UART write:
            sprintf(echoMsg, "%f\n\r", ambientLight);
            UART_write(uart, echoMsg, strlen(echoMsg));
            */

            programState = WAITING;
        }


        if (programState == RECEIVING_MESSAGE) {
            // TODO:
            // Viestin vastaanottaminen

            // Viesti vastaanotettu
            programState = MESSAGE_RECEIVED;
        }


        if (programState == MESSAGE_RECEIVED) {
            // TODO:

            // Jos oikeassa asennossa ja viestiä ei kirjoiteta
            // if ((asdf) && (programStateWriting == WAITING))
                // Näytetään viesti
                programState = SHOW_MESSAGE;
        }


        if (programState == SHOW_MESSAGE) {
            // TODO:
            // Viestin morsetus

            // kun valmis:
                programState = WAITING;
        }


        if (programState == READ_COMMANDS) {
            // TODO:
            // Lue komentoja

            // kun valmis:
                    {
                programState = SEND_MESSAGE;
                programStateWriting = SEND_MESSAGE;
            }
        }


        if (programState == READ_CHARACTERS) {
            // TODO:
            // Lue merkkejä

            if (fabs(gyro_x) > 150 && fabs(gyro_x) > fabs(gyro_y) && fabs(gyro_x) > fabs(gyro_z)) {
                sendToUART(".");  // gyroskoopin x-akselin yläraja
                Task_sleep(1000000 / Clock_tickPeriod);
            }
            if (fabs(gyro_y) > 150 && fabs(gyro_y) > fabs(gyro_x) && fabs(gyro_y) > fabs(gyro_z)) {
                sendToUART("-");  // gyroskoopin y-akselin yläraja
                Task_sleep(1000000 / Clock_tickPeriod);
            }
            if (fabs(gyro_z) > 150 && fabs(gyro_z) > fabs(gyro_y) && fabs(gyro_z) > fabs(gyro_x)) {
                sendToUART(" ");  // gyroskoopin z-akselin yläraja
                Task_sleep(1000000 / Clock_tickPeriod);
            }

            // kun valmis:
                    {
                programState = SEND_MESSAGE;
                programStateWriting = SEND_MESSAGE;
            }
        }


        if (programState == SEND_MESSAGE) {
            // TODO:
            // Lähetä viestit

            // Led0 (virheä) päälle
            programState = MESSAGE_SENT;
            programStateWriting = MESSAGE_SENT;
        }


        if (programState == MESSAGE_SENT) {
            // TODO:
            // Ajasta 2s

            // Jos viestejä vastaanotettu lähetyksen aikana:
            //if  receivedMessageBuffer ei tyhjä:
                // Siirry vastaanotetun viestin tilaan
                // programState = MESSAGE_RECEIVED;
            //else
            // Siirry takaisin odotustilaan
                programState = WAITING;
            programStateWriting = WAITING;
        }



        // Just for sanity check for exercise, you can comment this out
        System_printf("uartTask\n");
        System_flush();

        // Ohjelmataajuus:
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}




Void sensorTaskFxn(UArg arg0, UArg arg1) {

    I2C_Handle      i2c;
    I2C_Params      i2cParams;

    // Alustetaan i2c-väylä
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;

    // Avataan I2C väylä
    i2c = I2C_open(Board_I2C_TMP, &i2cParams);
    if (i2c == NULL) {
        System_abort("Error Initializing I2C\n");
    }

    float ax, ay, az, gx, gy, gz;

    // Haetaan data MPU-anturin avulla
    mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);

    // Päivitetään globaalit muuttujat ja skaalataan kiihtyvyys arvoille 100 ja gyroskoopin arvoille suoraan
    acl_x = ax * 100;
    acl_y = ay * 100;
    acl_z = az * 100;
    gyro_x = gx;
    gyro_y = gy;
    gyro_z = gz;

    System_printf("Accel: %d, %d, %d | Gyro: %d, %d, %d\n", (int)acl_x, (int)acl_y, (int)acl_z, (int)gyro_x, (int)gyro_y, (int)gyro_z);
    System_flush();


    /*
    // Alusta sensori OPT3001 setup-funktiolla
    // Laita ennen funktiokutsua eteen 100ms viive (Task_sleep)
    Task_sleep(10000 / Clock_tickPeriod);
    opt3001_setup(&i2c);

    while (1) {

        // Lue sensorilta dataa ja tulosta se Debug-ikkunaan merkkijonona

        double optData = opt3001_get_data(&i2c);
        /*char optDataStr[5];
        snprintf(optDataStr, 5, "%f", optData);
        System_printf(optDataStr);*/
/*
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
*/


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


    // Ledi 0 (virheä) käyttöön ohjelmassa
    led0Handle = PIN_open( &led0State, led0Config );
    if(!led0Handle) {
       System_abort("Error initializing LED pin\n");
    }
    // Ledi 1 (punainen) käyttöön
    led1Handle = PIN_open( &led1State, led1Config );
    if(!led1Handle) {
       System_abort("Error initializing LED pin\n");
    }


    // Painonappi 0 (toiminto) käyttöön ohjelmassa
    button0Handle = PIN_open(&button0State, button0Config);
    if(!button0Handle) {
       System_abort("Error initializing button0 pin\n");
    }

    // Painonappi 1 (on-off) käyttöön ohjelmassa
    button1Handle = PIN_open(&button1State, button1Config);
    if(!button1Handle) {
       System_abort("Error initializing button1 pin\n");
    }


    // MPU:n virtapin
    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL) {
        System_abort("Pin open failed!");
    }


    // Painonapille 0 keskeytyksen käsittellijä
    if (PIN_registerIntCb(button0Handle, &button0Fxn) != 0) {
       System_abort("Error registering button callback function");
    }

    // Painonapille 1 keskeytyksen käsittellijä
    if (PIN_registerIntCb(button1Handle, &button1Fxn) != 0) {
       System_abort("Error registering button callback function");
    }

    // Luo tehtävä sensorin lukemiseen
    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority=2;
    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    // Luodaan UARTin tehtävä
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
