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
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];
Char buzzerTaskStack[STACKSIZE];

// Message buffer size:
#define BUFFER_SIZE 128




//// RTOS-muuttujat käyttöön:

// Toimintonappi
static PIN_Handle button0Handle;
static PIN_State button0State;

// Virtakytkin
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

// Kaiutin
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
bool spacesWritten = 0;

// Buzzerin käyttöönottomuuttuja:
bool buzzerInUse = true;

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


//// JONOTIETORAKENNE:

// Jonotietorakenteen koko:
#define SENSORQUEUE_SIZE 21 // jonotietorakenne ottaa sisään arvoja SENSORQUEUE_SIZE-1 verran
#define SENSORAMOUNT 6

// Jonotietorakenne:
typedef struct {
    float data[SENSORAMOUNT][SENSORQUEUE_SIZE];
    int head;
    int tail;
} Queue;

//// Funktioiden esittely:

void initializeQueue(Queue* que);
bool isEmpty(Queue* que);
bool isFull(Queue* que);
void enqueue(Queue* que, float values[SENSORAMOUNT]);
void dequeue(Queue* que);
void peek(Queue* que, float values[SENSORAMOUNT]);
int queuePeek(Queue* que, float values[SENSORAMOUNT][SENSORQUEUE_SIZE]);
void printQueue(Queue* que);


// Jonotietorakenne anturidatalle
Queue sensorQueue;

// Jonotietorakenteen sensorivakiot:
enum sensorQueueConstants { ACL_X = 0,
                            ACL_Y,
                            ACL_Z,
                            GYRO_X,
                            GYRO_Y,
                            GYRO_Z
};



// JONOTIETORAKENTEEN FUNKTIOT:

// Jonotietorakenteen alustaminen
void initializeQueue(Queue* que) {
    que->head = 0;
    que->tail = 0;
}

// Jonotietorakenteen tarkistus: onko tyhjä?
bool isEmpty(Queue* que) {
    return (que->head == que->tail);
}

// Jonotietorakenteen tarkistus: onko täysi?
bool isFull(Queue* que) {
    return ((que->head - 1 == que->tail) || (que->head - 1 == que->tail - SENSORQUEUE_SIZE));
}

// Jonotietorakenteeseen lisääminen
void enqueue(Queue* que, float values[SENSORAMOUNT]) {

    if (isFull(que)) {
        System_printf("Queue is full\n");
        return;
    }


    int sensorIndex;
    for (sensorIndex = 0; sensorIndex < SENSORAMOUNT; sensorIndex++) {
        que->data[sensorIndex][que->tail] = values[sensorIndex];
    }

    que->tail++;

    if (que->tail >= SENSORQUEUE_SIZE) {
        que->tail -= SENSORQUEUE_SIZE;
    }

}

// Jonotietorakenteen ensimmäisen elementin poisto
void dequeue(Queue* que) {
    if (isEmpty(que)) {
        System_printf("Queue is empty\n");
        return;
    }

    que->head++;

    if (que->head >= SENSORQUEUE_SIZE) {
        que->head -= SENSORQUEUE_SIZE;
    }

}

// Jonotietorakenteen ensimmäisen elementin pyytäminen
void peek(Queue* que, float values[SENSORAMOUNT])
{
    if (isEmpty(que)) {
        System_printf("Queue is empty\n");
        System_flush();
        return;
    }

    int sensorIndex;
    int headIndex = que->head;
    if (headIndex < 0)
        headIndex += SENSORQUEUE_SIZE;

    for (sensorIndex = 0; sensorIndex < SENSORAMOUNT; sensorIndex++) {
        values[sensorIndex] = que->data[sensorIndex][headIndex + 1];
    }

}

// Koko jonotietorakenteen pyytäminen
int queuePeek(Queue* que, float values[SENSORAMOUNT][SENSORQUEUE_SIZE])
{
    if (isEmpty(que)) {
        System_printf("Queue is empty\n");
        System_flush();
        return -1;
    }


    int headIndex = que->head;

    if (headIndex > que->tail) {
        headIndex -= SENSORQUEUE_SIZE;
    }


    int index;
    int valueIndex = 0;
    for (index = headIndex; index < que->tail; index++) {
        int queueIndex;

        if (index < 0) {
            queueIndex = index + SENSORQUEUE_SIZE;
        }
        else
            queueIndex = index;



        int sensorIndex;

        for (sensorIndex = 0; sensorIndex < SENSORAMOUNT; sensorIndex++) {
            values[sensorIndex][valueIndex] = que->data[sensorIndex][queueIndex];
        }

        valueIndex++;
    }

    // Palauttaa lukumäärän, monta elementtiä liitettiin annettuun listaan
    return valueIndex;
}

// Tulostaa jonotietorakenteen sisällön
void printQueue(Queue* que)
{
    if (isEmpty(que)) {
        System_printf("Queue is empty\n");
        System_flush();
        return;
    }

    System_printf("Current Queue:\n");

    int headIndex = que->head;

    if (headIndex > que->tail) {
        headIndex -= SENSORQUEUE_SIZE;
    }

    int index;
    for (index = headIndex; index < que->tail; index++) {
        int queueIndex;

        if (index < 0) {
            queueIndex = index + SENSORQUEUE_SIZE;
        }
        else
            queueIndex = index;

        System_printf("Ax %d,\tAy %d,\tAz %d,\tGx %d,\tGy %d,\tGz %d\n",
                      (int)que->data[ACL_X][queueIndex], (int)que->data[ACL_Y][queueIndex], (int)que->data[ACL_Z][queueIndex],
                      (int)que->data[GYRO_X][queueIndex], (int)que->data[GYRO_Y][queueIndex], (int)que->data[GYRO_Z][queueIndex]);
    }
    System_printf("\n");
    System_flush();
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
    System_printf("Button 0 pressed\n");

    if (programState == WAITING) {
        programState = READ_CHARACTERS;
    }
    else if (programState == READ_CHARACTERS) {
        if (!charactersWritten)
            programState = READ_COMMANDS;
        else
            // TODO: resetoi kirjoitettu viesti!
            // resetMessage();
            // Merkitsee viestin tyhjäksi
            System_printf("Message reset (not implemented yet)\n");
            charactersWritten = false;
    }
}


// Käsittelijäfunktio virtanapille
Void powerFxn(PIN_Handle handle, PIN_Id pinId) {

   // Näyttö pois päältä
   // Display_clear(displayHandle);
   // Display_close(displayHandle);

   // Odotetaan hetki ihan varalta..
   Task_sleep(100000 / Clock_tickPeriod);

   // Taikamenot
   PIN_close(powerButtonHandle);
   buzzerClose(); // Suljetaan varalta kaiutin, jos ollut päällä
   PINCC26XX_setWakeup(powerButtonWakeConfig);
   Power_shutdown(NULL,0);
}


//Käsittelijäfunktio kaiuttimelle
Void buzzerTaskFxn(UArg arg0, UArg arg1) {

    // While loop tarkistaa, onko buzzerin käyttö estetty boolean-muuttujalla
    while (buzzerInUse) {
        buzzerOpen(hBuzzer);
        buzzerSetFrequency(261);
        Task_sleep(50000 / Clock_tickPeriod);
        buzzerSetFrequency(293);
        Task_sleep(50000 / Clock_tickPeriod);
        buzzerSetFrequency(329);
        Task_sleep(50000 / Clock_tickPeriod);
        buzzerSetFrequency(349);
        Task_sleep(50000 / Clock_tickPeriod);
        buzzerClose();

        Task_sleep(950000 / Clock_tickPeriod);
    }

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

    float q_gyro_x;
    float q_gyro_y;
    float q_gyro_z;
    float q_acl_x;
    float q_acl_y;
    float q_acl_z;


    // Odotetaan, että MPU-anturi käynnistyy
    Task_sleep(1000000 / Clock_tickPeriod);



    //// TILAKONE

    while (1) {


        if (programState == RECEIVING_MESSAGE) {
            System_printf("programState = RECEIVING_MESSAGE\n");
            System_flush();
            // TODO:
            // Viestin vastaanottaminen

            // Viesti vastaanotettu
            programState = MESSAGE_RECEIVED;
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

            bool commandSent = false;

            // Green led on:
            PIN_setOutputValue( led0Handle, Board_LED0, 1 );

            // Siirretään sensoridata taulukkoon ja tallennetaan sensoridatapisteiden määrä taulukossa
            float sensorData[SENSORAMOUNT][SENSORQUEUE_SIZE];
            int valueAmount = queuePeek(&sensorQueue, sensorData);

            // Käy läpi sensoridatan aloittaen vanhimmasta datasta
            int index;
            for (index = 0; index < valueAmount; index++) {

                q_gyro_x = sensorData[GYRO_X][index];
                q_gyro_y = sensorData[GYRO_Y][index];
                q_gyro_z = sensorData[GYRO_Z][index];
                q_acl_x = sensorData[ACL_X][index];
                q_acl_y = sensorData[ACL_Y][index];
                q_acl_z = sensorData[ACL_Z][index];


                // TODO: Oikeat komennot ja niiden tunnistus!
                // TODO: sendToUART korvataan addToMessage tms

                // KOMENTOJEN LUKU:

                if (fabs(q_gyro_x) > 150 && fabs(q_gyro_x) > fabs(q_gyro_y) && fabs(q_gyro_x) > fabs(q_gyro_z)) {
                    // gyroskoopin x-akselin yläraja "SOS"
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(" ");
                    sendToUART("-");
                    sendToUART("-");
                    sendToUART("-");
                    sendToUART(" ");
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(" ");
                    sendToUART(" ");
                    sendToUART(" ");
                    initializeQueue(&sensorQueue);
                    commandSent = true;
                    break;
                }
                if (fabs(q_gyro_y) > 150 && fabs(q_gyro_y) > fabs(q_gyro_x) && fabs(q_gyro_y) > fabs(q_gyro_z)) {
                    // gyroskoopin y-akselin yläraja "APUA"
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART(" ");
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART("-");
                    sendToUART(".");
                    sendToUART(" ");
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART(" ");
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART(" ");
                    sendToUART(" ");
                    sendToUART(" ");
                    initializeQueue(&sensorQueue);
                    commandSent = true;
                    break;
                }
                if (fabs(q_gyro_z) > 150 && fabs(q_gyro_z) > fabs(q_gyro_y) && fabs(q_gyro_z) > fabs(q_gyro_x)) {
                    // gyroskoopin z-akselin yläraja "HELP"
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(" ");
                    sendToUART(".");
                    sendToUART(" ");
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(" ");
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART("-");
                    sendToUART(".");
                    sendToUART(" ");
                    sendToUART(" ");
                    sendToUART(" ");
                    initializeQueue(&sensorQueue);
                    commandSent = true;
                    break;
                }

            }

            if (commandSent) {
                programState = SEND_MESSAGE;
                programStateWriting = SEND_MESSAGE;
            }
        }




        if (programState == READ_CHARACTERS) {
            System_printf("programState = READ_CHARACTERS\n");
            System_flush();

            // Blinking green led
            uint_t pinValue = PIN_getOutputValue( Board_LED0 );
            pinValue = !pinValue;
            PIN_setOutputValue( led0Handle, Board_LED0, pinValue );


            // Siirretään sensoridata taulukkoon ja tallennetaan sensoridatapisteiden määrä taulukossa
            float sensorData[SENSORAMOUNT][SENSORQUEUE_SIZE];
            int valueAmount = queuePeek(&sensorQueue, sensorData);

            // Käy läpi sensoridatan aloittaen vanhimmasta datasta
            int index;
            for (index = 0; index < valueAmount; index++) {

                q_gyro_x = sensorData[GYRO_X][index];
                q_gyro_y = sensorData[GYRO_Y][index];
                q_gyro_z = sensorData[GYRO_Z][index];
                q_acl_x = sensorData[ACL_X][index];
                q_acl_y = sensorData[ACL_Y][index];
                q_acl_z = sensorData[ACL_Z][index];


                // TODO: Lopulliset merkkien tunnistusliikkeet!
                // TODO: sendToUART korvataan addToMessage tms

                // MERKKIEN LUKU:

                if (fabs(q_gyro_x) > 150 && fabs(q_gyro_x) > fabs(q_gyro_y) && fabs(q_gyro_x) > fabs(q_gyro_z)) {
                    sendToUART(".");  // gyroskoopin x-akselin yläraja
                    initializeQueue(&sensorQueue);
                    charactersWritten = true;
                    spacesWritten = 0;
                    Task_sleep(1000000 / Clock_tickPeriod); // 1 s
                    break;
                }
                if (fabs(q_gyro_y) > 150 && fabs(q_gyro_y) > fabs(q_gyro_x) && fabs(q_gyro_y) > fabs(q_gyro_z)) {
                    sendToUART("-");  // gyroskoopin y-akselin yläraja
                    initializeQueue(&sensorQueue);
                    charactersWritten = true;
                    spacesWritten = 0;
                    Task_sleep(1000000 / Clock_tickPeriod); // 1 s
                    break;
                }
                if (fabs(q_gyro_z) > 150 && fabs(q_gyro_z) > fabs(q_gyro_y) && fabs(q_gyro_z) > fabs(q_gyro_x)) {
                    sendToUART(" ");  // gyroskoopin z-akselin yläraja
                    initializeQueue(&sensorQueue);
                    charactersWritten = true;
                    spacesWritten++;
                    Task_sleep(1000000 / Clock_tickPeriod); // 1 s
                    break;
                }

            }


            if (spacesWritten >= 3) {
                programState = SEND_MESSAGE;
                programStateWriting = SEND_MESSAGE;
            }
        }




        if (programState == SEND_MESSAGE) {
            System_printf("programState = SEND_MESSAGE\n");
            System_flush();

            // TODO:
            // Lähetä viestit sendToUART:illa vasta tässä vaiheessa!

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
        Task_sleep(200000 / Clock_tickPeriod); // 5 Hz
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


        // Valmistellaan data jonotietorakennetta varten:
        float sensorData[SENSORAMOUNT];
        sensorData[ACL_X] = acl_x;
        sensorData[ACL_Y] = acl_y;
        sensorData[ACL_Z] = acl_z;
        sensorData[GYRO_X] = gyro_x;
        sensorData[GYRO_Y] = gyro_y;
        sensorData[GYRO_Z] = gyro_z;

        // Jos jono täynnä, poista vanhin arvo
        if (isFull(&sensorQueue)) {
            dequeue(&sensorQueue);
        }

        // Lisää viimeisin sensoridata
        enqueue(&sensorQueue, sensorData);

        // Tulosta arvot jonotietorakenteesta
        // printQueue(&sensorQueue);

        // Vaihdetaan led-pinnin tilaa negaatiolla näytteenottotaajuuden visualisoimiseksi
        // uint_t pinValue = PIN_getOutputValue( Board_LED1 );
        // pinValue = !pinValue;
        // PIN_setOutputValue( led1Handle, Board_LED1, pinValue );


        // 100000 = 0,1 sekunnin viive = 10 Hz
        Task_sleep(50000 / Clock_tickPeriod); // 20 Hz
    }

    /*
    // Alusta sensori OPT3001 setup-funktiolla
    // Laita ennen funktiokutsua eteen 100ms viive (Task_sleep)
    Task_sleep(10000 / Clock_tickPeriod);
    opt3001_setup(&i2c);

    while (1) {

        // Lue sensorilta dataa ja tulosta se Debug-ikkunaan merkkijonona

        double optData = opt3001_get_data(&i2c);
        //char optDataStr[5];
        //snprintf(optDataStr, 5, "%f", optData);
        //System_printf(optDataStr);

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

    // Alustetaan sensorijonotietorakenne
    initializeQueue(&sensorQueue);


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

    // Virtakytkimelle keskeytyksen käsittelijä
    if (PIN_registerIntCb(powerButtonHandle, &powerFxn) != 0) {
       System_abort("Error registering power button callback");
    }

    // Luodaan tehtävä kaiuttimen äänentoistolle
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

    // Akun varauksen näyttäminen:
    System_printf("Battery voltage: %ld\n",patteri/256);
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
