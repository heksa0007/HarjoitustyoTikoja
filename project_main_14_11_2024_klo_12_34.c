/* C Standard library */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

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

//battery
#include <driverlib/aon_batmon.h>

/* Board Header files */
#include "Board.h"
#include "sensors/opt3001.h"
#include "sensors/mpu9250.h"

//Kaiutin
#include "buzzer.h"

/* Task */
#define STACKSIZE 2048
#define BUFFER_SIZE 128  // Määritellään puskuriin mahtuvien merkkien maksimimäärä
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];
Char buzzerTaskStack[STACKSIZE];

// Message buffer size:
#define BUFFER_SIZE 128

//// RTOS-muuttujat käyttöön:

// Napit
static PIN_Handle button0Handle;
static PIN_State button0State;

//Virtakytkin
static PIN_Handle powerButtonHandle;
static PIN_State powerButtonState;

//Kaiutin
static PIN_Handle hBuzzer;
static PIN_State sBuzzer;

// Ledit
static PIN_Handle led0Handle;
static PIN_State led0State;
static PIN_Handle led1Handle;
static PIN_State led1State;
// MPU power pin global variables (kiihtyvyysanturi)
static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;

// Puskuri UART-merkkejä varten
char uartBuffer[BUFFER_SIZE];
int bufferIndex = 0;  // Seuraa seuraavaa vapaata paikkaa puskurissa



//// Alustetaan pinniconfiguraatiot:

// Painonappi 0 (toiminto)
PIN_Config button0Config[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};

// Virtakytkin. Samalle painonapille kaksi erilaista konfiguraatiota
PIN_Config powerButtonConfig[] = {
   Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};
PIN_Config powerButtonWakeConfig[] = {
   Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PINCC26XX_WAKEUP_NEGEDGE,
   PIN_TERMINATE
};

//Kaiutin
PIN_Config cBuzzer[] = {
  Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
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



// MPU I2C configuration
static const I2CCC26XX_I2CPinCfg i2cCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};



// Tilakoneen esittely
enum state { WAITING=1,
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
bool charactersWritten = false;


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
I2C_Handle i2c;

// Funktio, joka sytyttää LEDin lyhyeksi tai pitkäksi ajaksi
void flashLED(UArg arg0, UArg arg1, int duration) {
    PIN_setOutputValue(led1Handle, Board_LED1, 1); // Sytytä LED
    Task_sleep(duration); // Viive millisekunteina
    PIN_setOutputValue(led1Handle, Board_LED1, 0); // Sammuta LED
}

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

    if (programState == WAITING) {
        programState = RECEIVING_MESSAGE;;
    }
    else if (programState == READ_CHARACTERS) {
        if (!charactersWritten)
            programState = READ_COMMANDS;
    }


}

//Käsittelijäfunktio kaiuttimelle
Void buzzerTaskFxn(UArg arg0, UArg arg1) {

  while (1) {
    buzzerOpen(hBuzzer);
    buzzerSetFrequency(0);
    Task_sleep(50000 / Clock_tickPeriod);
    buzzerClose();

    Task_sleep(950000 / Clock_tickPeriod);
  }

}

// Käsittelijäfunktio virtanapille
Void powerFxn(PIN_Handle handle, PIN_Id pinId) {

   // Näyttö pois päältä
   // Display_clear(displayHandle);
   // Display_close(displayHandle);

   // Odotetana hetki ihan varalta..
   Task_sleep(100000 / Clock_tickPeriod);

   // Taikamenot
   PIN_close(powerButtonHandle);
   PINCC26XX_setWakeup(powerButtonWakeConfig);
   Power_shutdown(NULL,0);
}
/*
// Käsittelijäfunktio UART
static void uartFxn(UART_Handle handle, void *rxBuf, size_t len) {
    System_printf("programState = RECEIVING_MESSAGE\n");
    System_flush();

    // Tila päivitetään
    programState = RECEIVING_MESSAGE;

}*/

/* Task Functions */
Void uartTaskFxn(UArg arg0, UArg arg1) {

    int countSpace = 0;
    // UARTin alustus

    UART_Handle handle;
    UART_Params params;
    char receivedChar;

    // UART-parametrien alustaminen
    UART_Params_init(&params);
    params.readMode = UART_MODE_BLOCKING;
    params.readDataMode = UART_DATA_TEXT;
    params.writeDataMode = UART_DATA_TEXT;
    params.readEcho = UART_ECHO_ON;
    params.baudRate = 9600;

    // UART käyttöön ohjelmassa
   handle = UART_open(Board_UART, &params);
   if (handle == NULL) {
      System_abort("Error opening the UART");
   }
   System_printf("Waiting for UART input...\n");
   System_flush();

    // Odotetaan, että MPU-anturi käynnistyy
    Task_sleep(1000000 / Clock_tickPeriod);

    while (1) {


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
        UART_read(uart, &receivedChar, 1);  // Lue yksi merkki UARTin kautta

        // Tallenna merkki puskuriin, jos tilaa on
                    if (bufferIndex < BUFFER_SIZE - 1) {
                        uartBuffer[bufferIndex++] = receivedChar;
                        uartBuffer[bufferIndex] = '\0';  // Päivitetään nollaterminaattori
                    }
                    else {
                        System_printf("Buffer full, character discarded: '%c'\n", receivedChar);
                        System_flush();
                    }
        //// TILAKONE


        if (programState == RECEIVING_MESSAGE) {

            System_printf("programState = RECEIVING_MESSAGE\n");
            System_flush();

            //TODO
            //viestin vastaanottaminen, countSpace
            // Tarkista puskurin tila ja tallenna merkki

                // Sytytä LED eri pituiseksi ajaksi riippuen merkistä
                if (receivedChar == '.') {
                    flashLED(arg0, arg1, 500);  // Lyhyt välähdys (50 ms)
                    countSpace = 0;
                } else if (receivedChar == '-') {
                    flashLED(arg0, arg1, 10000);  // Pitkä välähdys (150 ms)
                    countSpace = 0;
                } else if (receivedChar == ' ') {
                    Task_sleep(1000); // 1 sekunnin tauko, jos vastaanotetaan välilyönti
                    countSpace += 1;
                }

            // Viesti vastaanotettu
            if(countSpace >= 3){
                countSpace = 0;
                programState = MESSAGE_RECEIVED;
            }
        }




        if (programState == MESSAGE_RECEIVED) {
            System_printf("programState = MESSAGE_RECEIVED\n");
            System_flush();
            // TODO:

            // Jos oikeassa asennossa ja viestiä ei kirjoiteta
            // if ((asdf) && (programStateWriting == WAITING))
                // Näytetään viesti
                programState = SHOW_MESSAGE;
        }


        if (programState == SHOW_MESSAGE) {
            System_printf("programState = SHOW_MESSAGE\n");
            System_flush();
            // TODO:
            // Viestin morsetus

            // kun valmis:
                programState = WAITING;
        }


        if (programState == READ_COMMANDS) {
            System_printf("programState = READ_COMMANDS\n");
            System_flush();

            // Green led on:
            PIN_setOutputValue( led0Handle, Board_LED0, 1 );

            // TODO:
            // Lue komentoja

            // kun valmis:
                    /*{
                programState = SEND_MESSAGE;
                programStateWriting = SEND_MESSAGE;
            }*/
        }


        if (programState == READ_CHARACTERS) {
            System_printf("programState = READ_CHARACTERS\n");
            System_flush();

            // Blinking green led
            uint_t pinValue = PIN_getOutputValue( Board_LED0 );
            pinValue = !pinValue;
            PIN_setOutputValue( led0Handle, Board_LED0, pinValue );

            // TODO:
            // Lue merkkejä

            // kun merkki luettu:
            // charactersWritten = true;

            // kun valmis:
                    /*{
                programState = SEND_MESSAGE;
                programStateWriting = SEND_MESSAGE;
            }*/
        }


        if (programState == SEND_MESSAGE) {
            System_printf("programState = SEND_MESSAGE\n");
            System_flush();
            // TODO:
            // Lähetä viestit

            // ei lähettämättömiä merkkejä kirjoitettu
            charactersWritten = false;

            // Led0 (virheä) päälle
            programState = MESSAGE_SENT;
            programStateWriting = MESSAGE_SENT;
        }


        if (programState == MESSAGE_SENT) {
            System_printf("programState = MESSAGE_SENT\n");
            System_flush();
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
        //System_printf("uartTask\n");
        //System_flush();

        // Ohjelmataajuus (100000 = 0.1s = 10 Hz):
        Task_sleep(50000 / Clock_tickPeriod);
    }
}




Void sensorTaskFxn(UArg arg0, UArg arg1) {

    // Väliaikaiset datamuuttujat
    float ax, ay, az, gx, gy, gz;

    I2C_Handle      i2c;
    I2C_Params      i2cParams;

    // Alustetaan i2c-väylä
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;
    i2cParams.custom = (uintptr_t)&i2cCfg;

    // MPU power on
    PIN_setOutputValue(hMpuPin, Board_MPU_POWER, Board_MPU_POWER_ON);

    // Odotetaan, että MPU-anturi käynnistyy
    Task_sleep(100000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();

    // Avataan I2C väylä
    i2c = I2C_open(Board_I2C_TMP, &i2cParams);
    if (i2c == NULL) {
        System_abort("Error Initializing I2C\n");
    }

    // MPU:n asetukset ja kalibrointi
    mpu9250_setup(&i2c);
    System_printf("MPU9250: Setup and calibration OK\n");
    System_flush();



    // Sensorinlukusilmukka:

    while (1) {
        // Haetaan data MPU-anturin avulla
        mpu9250_get_data(&i2c, &ax, &ay, &az, &gx, &gy, &gz);

        // Päivitetään globaalit muuttujat ja skaalataan kiihtyvyys arvoille 100 ja gyroskoopin arvoille suoraan
        acl_x = ax * 100;
        acl_y = ay * 100;
        acl_z = az * 100;
        gyro_x = gx;
        gyro_y = gy;
        gyro_z = gz;

        System_printf("Accel: %d, %d, %d | Gyro: %d, %d, %d\n", (int)acl_x, (int)acl_y, (int)acl_z, (int)gyro_x, (int)gyro_y, (int)gyro_z);
        System_flush();

        Task_sleep(50000 / Clock_tickPeriod);  // 100000 = 0,1 sekunnin viive
    }

    /*
    // Alusta sensori OPT3001 setup-funktiolla
    // Laita ennen funktiokutsua eteen 100ms viive (Task_sleep)
    Task_sleep(10000 / Clock_tickPeriod);
    opt3001_setup(&i2c);

    while (1) {

        // Lue sensorilta dataa ja tulosta se Debug-ikkunaan merkkijonona

        double optData = opt3001_get_data(&i2c);
        char optDataStr[5];
        snprintf(optDataStr, 5, "%f", optData);
        System_printf(optDataStr);

        // JTKJ: Tehtävä 3. Tallenna mittausarvo globaaliin muuttujaan
        //       Muista tilamuutos

        ambientLight = optData;
        programState = DATA_READY;

        // Just for sanity check for exercise, you can comment this out
        System_printf("sensorTask\n");
        System_flush();

        // Once per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }*/
}




Int main(void) {
    uint32_t patteri = HWREG(AON_BATMON_BASE + AON_BATMON_O_BAT);

    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;
    Task_Handle buzzerTask;
    Task_Params buzzerTaskParams;


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
    /*
    // Painonappi 1 (on-off) käyttöön ohjelmassa
    button1Handle = PIN_open(&button1State, button1Config);
    if(!button1Handle) {
       System_abort("Error initializing button1 pin\n");
    }
    */

    //Virtakytkin käyttöön ohjelmassa
    powerButtonHandle = PIN_open(&powerButtonState, powerButtonConfig);
    if(!powerButtonHandle) {
       System_abort("Error initializing power button\n");
    }

    // Buzzer
    hBuzzer = PIN_open(&sBuzzer, cBuzzer);
    if (hBuzzer == NULL) {
      System_abort("Pin open failed!");
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

    //Virtakytkimelle keskeytyksen käsittelijä
    if (PIN_registerIntCb(powerButtonHandle, &powerFxn) != 0) {
       System_abort("Error registering power button callback");
    }

    //Luodaan tehtävä kaiuttimen äänentoistolle
    Task_Params_init(&buzzerTaskParams);
    buzzerTaskParams.stackSize = STACKSIZE;
    buzzerTaskParams.stack = &buzzerTaskStack;
    buzzerTask = Task_create((Task_FuncPtr)buzzerTaskFxn, &buzzerTaskParams, NULL);
    if (buzzerTask == NULL) {
      System_abort("Task create failed!");
    }

    // Luodaan tehtävä sensorin lukemiseen
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
    System_printf("Battery voltage: %ld\n",patteri/256);
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
