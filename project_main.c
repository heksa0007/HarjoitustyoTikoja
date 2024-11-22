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
             MESSAGE_RECEIVED, SHOW_MESSAGE};

enum state programState = WAITING;


// Tallennusmuuttuja vastaanotetuille viesteille
uint8_t uartBuffer[BUFFER_SIZE];
uint8_t receivedMessageBuffer[BUFFER_SIZE];
uint8_t writtenMessageBuffer[BUFFER_SIZE];
int receivedMessageBufferIndex = 0;
int writtenIndex = 0;
int spacesWritten = 0;
bool writtenMessageBufferOverload = false;
bool messageReceived = false;
bool charactersWritten = false;

// Buzzerin käyttöönottomuuttuja:
bool buzzerInUse = false;


// Valoisuuden globaali muuttuja
double ambientLight = -1000;


// kiihtyvyyden ja gyroskooppien globaalit muuttujat (float-tyyppisenä)
float acl_x = 0.0;
float acl_y = 0.0;
float acl_z = 0.0;
float gyro_x = 0.0;
float gyro_y = 0.0;
float gyro_z = 0.0;

// SOS merkin tarkistus muuttujat
int pointState1 = 0, pointState = 0, lineState = 0;
UInt32 lastMessageTime = 0;

// UART- ja I2C-kahvat
UART_Handle uart;
I2C_Handle i2c;

//Funktio, joka soittaa musiikkia, Tetris theme, Hirokazu Tanakan säveltämä (1989) (Riku)
void playMusic1() {

    buzzerOpen(hBuzzer);

    // Aloitetaan musiikin soittaminen
    buzzerSetFrequency(330); // E4
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(247); // B3
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(261); // C4
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(294); // D4
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(261); // C4
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(247); // B3
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(440); // A4
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(440); // A4
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(261); // C4
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(330); // E4
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(294); // D4
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(261); // C4
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(247); // B3
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(247); // B3
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(261); // C4
    Task_sleep(150000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(300000 / Clock_tickPeriod);

    buzzerSetFrequency(294); // D4
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(330); // E4
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(261); // C4
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(440); // A4
    Task_sleep(300000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(600000 / Clock_tickPeriod);

    buzzerSetFrequency(440); // A4
    Task_sleep(600000 / Clock_tickPeriod);
    buzzerSetFrequency(0);
    Task_sleep(1200000 / Clock_tickPeriod);

    // Lopetetaan musiikki
    buzzerSetFrequency(0);
    buzzerClose();
}


void resetSound() {
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




// Funktio, joka tarkistaa SOS-viestin ehdot  (Heikki)
void checkSOSCondition() {
    if (pointState == 3 && pointState1 == 3 && lineState == 3) {
        playMusic1();
        System_printf("SOS detected!\n");   // POISTAA SAA
        System_flush();                     // POISTAA SAA
        // Nollataan tilamuuttujat
        pointState = 0;
        lineState = 0;
        pointState1 = 0;
    }
}

// POISTAA SAA  koko tuleva funktio jonotietorakenteeseen saakka sillä tätä ei minun mielestä enää oikeasti käytetä kun muutin nollaustapaa
// Funktio, joka tarkistaa viestien vastaanottoajan ja nollaa pointStaten, jos viestiä ei tule sekunnin sisällä
Void monitorTaskFxn(UArg arg0, UArg arg1) {
    while (1) {
        UInt32 currentTime = Clock_getTicks();
        if ((currentTime - lastMessageTime) > (6000000 / Clock_tickPeriod)) {
            pointState = 0;
            lineState = 0;
            pointState1 = 0;
        }
        Task_sleep(500);  // Tarkista 0,5 sekunnin välein
    }
}

//// JONOTIETORAKENNE (Arttu):

// Jonotietorakenteen koko:
#define SENSORQUEUE_SIZE 65 // jonotietorakenne ottaa sisään arvoja SENSORQUEUE_SIZE-1 verran
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






// Funktio yksittäisen symbolin lähettämiseen UARTin kautta (Heikki)
void sendToUART(const char* symbol) {
    char message[5];
    sprintf(message, "%s\r\n\0", symbol);  // Muodostaa viestin, joka sisältää 4 tavua
    UART_write(uart, message, strlen(message) + 1);  // Lähetetään 4 tavua (mukaan lukien \0)
    Task_sleep(100000 / Clock_tickPeriod);  // Pieni viive viestien välillä
}





// Painonappien RTOS-muuttujat ja alustus

// Käsittelijämuuttuja toimintonapille
void button0Fxn(PIN_Handle handle, PIN_Id pinId) {
    if (programState == WAITING) {
        initializeQueue(&sensorQueue);
        programState = READ_CHARACTERS;
    }
    else if (programState == READ_CHARACTERS) {
        initializeQueue(&sensorQueue);
        programState = READ_COMMANDS;
        charactersWritten = false;
        writtenIndex = 0;
    }
    else if (programState == READ_COMMANDS) {
        programState = WAITING;
    }
}


// Käsittelijäfunktio virtanapille (Riku)
Void powerFxn(PIN_Handle handle, PIN_Id pinId) {
   // Odotetaan hetki ihan varalta..
   Task_sleep(100000 / Clock_tickPeriod);

   // Taikamenot
   PIN_close(powerButtonHandle);
   buzzerClose(); // Suljetaan varalta kaiutin, jos ollut päällä
   PIN_setOutputValue( led0Handle, Board_LED0, 0 );
   PIN_setOutputValue( led1Handle, Board_LED1, 0 );
   PINCC26XX_setWakeup(powerButtonWakeConfig);
   Power_shutdown(NULL,0);
}



// Käsittelijäfunktio viestin vastaanottamiselle
static void uartFxn(UART_Handle handle, uint8_t *rxBuf, size_t len) {

   // Nyt meillä on siis haluttu määrä merkkejä käytettävissä
   // rxBuf-taulukossa, pituus len, jota voimme käsitellä halutusti

   int index;
   for (index = 0; index < len; index++) {
       if (receivedMessageBufferIndex < BUFFER_SIZE - 1) {
           receivedMessageBuffer[receivedMessageBufferIndex] = rxBuf[index];
           receivedMessageBufferIndex++;
           receivedMessageBuffer[receivedMessageBufferIndex] = '\0';  // Päivitetään nollaterminaattori
           messageReceived = true;
           PIN_setOutputValue( led1Handle, Board_LED1, 1 ); // Punainen ledi päälle, kun on vastaanotettu viesti
       }
       else {
           System_printf("Buffer full, character discarded: '%c'\n", rxBuf[index]);
           System_flush();
       }
   }

   if (programState == WAITING)
       programState = MESSAGE_RECEIVED;


   // Käsittelijän viimeisenä asiana siirrytään odottamaan uutta keskeytystä..
   UART_read(handle, rxBuf, 1);
}



// Funktio, joka sytyttää LEDin lyhyeksi tai pitkäksi ajaksi
void flashLED1(UArg arg0, UArg arg1, int duration) {
    PIN_setOutputValue(led1Handle, Board_LED1, 1); // Sytytä LED
    Task_sleep(duration); // Viive millisekunteina
    PIN_setOutputValue(led1Handle, Board_LED1, 0); // Sammuta LED
}


/* Task Functions */

//UART:in käsittelijäfunktio
Void uartTaskFxn(UArg arg0, UArg arg1) {

    // UARTin alustus: 9600,8n1

    UART_Params uartParams;

    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.readMode = UART_MODE_CALLBACK;
    uartParams.readCallback = &uartFxn; // Käsittelijäfunktio
    uartParams.baudRate = 9600; // nopeus 9600baud
    uartParams.dataLength = UART_LEN_8; // 8
    uartParams.parityType = UART_PAR_NONE; // n
    uartParams.stopBits = UART_STOP_ONE; // 1

    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL) {
        System_abort("Error opening the UART");
    }

    // Aloitetaan vastaanotettavien viestien odotus:
    UART_read(uart, uartBuffer, 1);


    float q_gyro_x;
    float q_gyro_y;
    float q_gyro_z;
    //float q_acl_x;
    //float q_acl_y;
    //float q_acl_z;


    // Odotetaan, että MPU-anturi käynnistyy
    Task_sleep(1000000 / Clock_tickPeriod);



    //// TILAKONE (rakenne: Arttu)

    while (1) {

        if (programState == WAITING) {

            PIN_setOutputValue( led0Handle, Board_LED0, 0 );

        }


        if (programState == MESSAGE_RECEIVED) {

            // Jos oikeassa asennossa (Heikki)
            if (acl_z > 50 && acl_z < 90 && acl_x < 90 && acl_x > 50 && acl_y < 20 && acl_y > 0) {
                PIN_setOutputValue( led1Handle, Board_LED1, 0 ); // Punainen ledi pois päältä ennen viestin näyttämistä
                Task_sleep(1000000 / Clock_tickPeriod); // 1 sekunnin tauko ennen viestin näyttämistä
                // Näytetään viesti
                programState = SHOW_MESSAGE;
            }
        }



        if (programState == SHOW_MESSAGE) {
            //Uartilta sensortagille tulevien merkkien tunnistus ja sos merkkiä varten olevan tunnistus systeemi(Heikki)
            int index;
            for (index = 0; index < receivedMessageBufferIndex; index++) {
                // Sytytä LED eri pituiseksi ajaksi riippuen merkistä
                if (receivedMessageBuffer[index] == '.') {
                    flashLED1(arg0, arg1, 10000 / Clock_tickPeriod);  // Lyhyt välähdys (0.01 s)
                    if (lineState == 3 && pointState == 3){
                        pointState1++; //SOS merkin tarkistus ...
                    }
                    else {
                        pointState++; //SOS merkin tarkistus toinen ...
                    }
                } else if (receivedMessageBuffer[index] == '-') {
                    flashLED1(arg0, arg1, 500000 / Clock_tickPeriod);  // Pitkä välähdys (0.5 s)
                    if (pointState == 3) // Jos jo tullut 3 pistettä
                        lineState++; //SOS merkin tarkistus ---
                    else {
                        pointState = 0;
                        lineState = 0;
                        pointState1 = 0;
                    }
                } else if (receivedMessageBuffer[index] == ' ') {
                    Task_sleep(500000 / Clock_tickPeriod); // 0.5 sekunnin tauko, jos vastaanotetaan välilyönti
                }

                // 0.25 sekunnin tauko jokaisen merkin jälkeen
                Task_sleep(250000 / Clock_tickPeriod);
            }

            receivedMessageBufferIndex = 0;
            messageReceived = false;

            // Tarkistetaan SOS-viestin ehdot
            checkSOSCondition();
            pointState = 0;
            lineState = 0;
            pointState1 = 0;

            programState = WAITING;
        }




        if (programState == READ_COMMANDS) {

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
                //q_acl_x = sensorData[ACL_X][index];
                //q_acl_y = sensorData[ACL_Y][index];
                //q_acl_z = sensorData[ACL_Z][index];


                // TODO: Oikeat komennot ja niiden tunnistus!
                // TODO: sendToUART korvataan addToMessage tms

                // KOMENTOJEN LUKU:
                // Komentojen luku ehdot (Heikki)
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
                    initializeQueue(&sensorQueue);
                    commandSent = true;
                    break;
                }
                if (fabs(q_gyro_y) > 150 && fabs(q_gyro_y) > fabs(q_gyro_x) && fabs(q_gyro_y) > fabs(q_gyro_z)) {
                    // gyroskoopin y-akselin yläraja "ATTACK"
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART(" ");
                    sendToUART("-");
                    sendToUART(" ");
                    sendToUART("-");
                    sendToUART(" ");
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART(" ");
                    sendToUART("-");
                    sendToUART(".");
                    sendToUART("-");
                    sendToUART(".");
                    sendToUART(" ");
                    sendToUART("-");
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
                    // gyroskoopin z-akselin yläraja "IRTI"
                    sendToUART(".");
                    sendToUART(".");
                    sendToUART(" ");

                    sendToUART(".");
                    sendToUART("-");
                    sendToUART(".");
                    sendToUART(" ");

                    sendToUART("-");
                    sendToUART(" ");

                    sendToUART(".");
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
            }
        }




        if (programState == READ_CHARACTERS) {

            // Blinking green led
            uint_t pinValue = PIN_getOutputValue( Board_LED0 );
            pinValue = !pinValue;
            PIN_setOutputValue( led0Handle, Board_LED0, pinValue );

            if (writtenIndex != 0) {
                if ((ambientLight >= 0) && (ambientLight < 5)) {

                    charactersWritten = false;
                    writtenIndex = 0;
                    // Soita reset-ääni
                    resetSound();
                }
            }


            // Siirretään sensoridata taulukkoon ja tallennetaan sensoridatapisteiden määrä taulukossa
            float sensorData[SENSORAMOUNT][SENSORQUEUE_SIZE];
            int valueAmount = queuePeek(&sensorQueue, sensorData);

            // Käy läpi sensoridatan aloittaen vanhimmasta datasta
            int index;
            for (index = 0; index < valueAmount; index++) {

                q_gyro_x = sensorData[GYRO_X][index];
                q_gyro_y = sensorData[GYRO_Y][index];
                q_gyro_z = sensorData[GYRO_Z][index];
                //q_acl_x = sensorData[ACL_X][index];
                //q_acl_y = sensorData[ACL_Y][index];
                //q_acl_z = sensorData[ACL_Z][index];


                // MERKKIEN LUKU:
                // Merkkien luku ehdot (Heikki)
                if (fabs(q_gyro_x) > 150 && fabs(q_gyro_x) > fabs(q_gyro_y) && fabs(q_gyro_x) > fabs(q_gyro_z)) {
                    // gyroskoopin x-akselin yläraja
                    if (writtenIndex < BUFFER_SIZE) {
                        writtenMessageBuffer[writtenIndex] = 1; // "." = 1
                        writtenIndex++;
                    }
                    else {
                        writtenMessageBufferOverload = true;
                        programState = SEND_MESSAGE;
                    }

                    charactersWritten = true;
                    spacesWritten = 0;
                    buzzerOpen(hBuzzer);
                    buzzerSetFrequency(392);
                    Task_sleep(100000 / Clock_tickPeriod);
                    buzzerClose();
                    Task_sleep((1000000-100000) / Clock_tickPeriod); // kokonaiskesto 1 s
                    initializeQueue(&sensorQueue);
                    break;
                }
                if (fabs(q_gyro_y) > 150 && fabs(q_gyro_y) > fabs(q_gyro_x) && fabs(q_gyro_y) > fabs(q_gyro_z)) {
                    // gyroskoopin y-akselin yläraja

                    if (writtenIndex < BUFFER_SIZE) {
                        writtenMessageBuffer[writtenIndex] = 2; // "-" = 2
                        writtenIndex++;
                    }
                    else {
                        writtenMessageBufferOverload = true;
                        programState = SEND_MESSAGE;
                    }

                    charactersWritten = true;
                    spacesWritten = 0;
                    buzzerOpen(hBuzzer);
                    buzzerSetFrequency(329.63);
                    Task_sleep(300000 / Clock_tickPeriod);
                    buzzerClose();
                    Task_sleep((1000000-300000) / Clock_tickPeriod); // kokonaiskesto 1 s
                    initializeQueue(&sensorQueue);
                    break;
                }
                if (fabs(q_gyro_z) > 150 && fabs(q_gyro_z) > fabs(q_gyro_y) && fabs(q_gyro_z) > fabs(q_gyro_x)) {
                    // gyroskoopin z-akselin yläraja
                    if (writtenIndex < BUFFER_SIZE) {
                        writtenMessageBuffer[writtenIndex] = 0; // " " = 0
                        writtenIndex++;
                    }
                    else {
                        writtenMessageBufferOverload = true;
                        programState = SEND_MESSAGE;
                    }

                    charactersWritten = true;
                    spacesWritten++;
                    buzzerOpen(hBuzzer);
                    buzzerSetFrequency(261.63);
                    Task_sleep(500000 / Clock_tickPeriod);
                    buzzerClose();
                    Task_sleep((1000000-500000) / Clock_tickPeriod); // kokonaiskesto 1 s
                    initializeQueue(&sensorQueue);
                    break;
                }

            }


            if (spacesWritten >= 2) {
                spacesWritten = 0;
                programState = SEND_MESSAGE;
            }
        }




        if (programState == SEND_MESSAGE) {

            // Jos viestibuffer täynnä, poista viimeinen keskeneräinen kirjain!
            if (writtenMessageBufferOverload) {
                while (writtenMessageBuffer[writtenIndex-1] != 0) {
                    writtenIndex--;
                }
            }

            // Lähetä viestit sendToUART:illa vasta tässä vaiheessa!
            int index;
            for (index = 0; index < writtenIndex; index++) {
                switch (writtenMessageBuffer[index]){
                case 0:
                    sendToUART(" ");
                    break;
                case 1:
                    sendToUART(".");
                    break;
                case 2:
                    sendToUART("-");
                    break;
                }
            }

            // Varalta jos kyseessä overload, lähetetään viestin päättävä välilyönti:
            if (writtenMessageBufferOverload)
                sendToUART(" ");

            writtenMessageBufferOverload = false;
            writtenIndex = 0;

            // ei lähettämättömiä merkkejä kirjoitettu
            charactersWritten = false;

            // Led0 (virheä) päälle
            PIN_setOutputValue( led0Handle, Board_LED0, 1 );
            programState = MESSAGE_SENT;
        }



        if (programState == MESSAGE_SENT) {

            // Ajasta 2s
            Task_sleep((2000000) / Clock_tickPeriod);
            // Led0 (vihreä) pois päältä
            PIN_setOutputValue( led0Handle, Board_LED0, 0 );

            // Jos viestejä vastaanotettu lähetyksen aikana:
            if (messageReceived) {
                // Siirry vastaanotetun viestin tilaan
                programState = MESSAGE_RECEIVED;
            }
            else {
                // Siirry takaisin odotustilaan
                programState = WAITING;
            }
        }

        // Ohjelmataajuus (100000 = 0.1s = 10 Hz):
        Task_sleep(200000 / Clock_tickPeriod); // 5 Hz
    }
}



//Anturien käsittelijäfunktio. (Riku: valoanturin lisääminen ohjelmaan)
Void sensorTaskFxn(UArg arg0, UArg arg1) {
    int toggle = 0;
    // Väliaikaiset datamuuttujat
    float ax, ay, az, gx, gy, gz;

    // Own i2c-interface for MPU9250 sensor
    I2C_Handle      i2c;
    I2C_Params      i2cParams;
    //OPT
    I2C_Handle i2cOPT;
    I2C_Params i2cOPTParams;


    // Alustetaan i2c-väylä
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;
    i2cParams.custom = (uintptr_t)&i2cCfg;

    // Alustetaan i2c-väylä OPT
    I2C_Params_init(&i2cOPTParams);
    i2cParams.bitRate = I2C_400kHz;

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

    // Suljetaan MPU:n I2C-väylä
    I2C_close(i2c);
    System_printf("MPU9250: I2C closed\n");
    System_flush();

    // Avataan OPT-anturin I2C-väylä
    i2cOPT = I2C_open(Board_I2C_TMP, &i2cOPTParams);
    if (i2cOPT == NULL) {
        System_abort("Error Initializing I2C for OPT\n");
    }

    // OPT-anturin asetukset
    opt3001_setup(&i2cOPT);
    System_printf("OPT3001: Setup OK\n");
    System_flush();

    // Suljetaan OPT-anturin I2C-väylä
    I2C_close(i2cOPT);
    System_printf("OPT3001: I2C closed\n");
    System_flush();


    // Sensorinlukusilmukka:

    while (1) {


        // Haetaan data MPU-anturin avulla
        if(toggle == 0) {
            I2C_open(Board_I2C_TMP, &i2cParams);
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


            I2C_close(i2c);
        }
        else {
            //Avataan OPT I2C väylä
            i2cOPT = I2C_open(Board_I2C_TMP, &i2cOPTParams);
            //Luetaan OPT dataa ja tallennetaan globaaliin muuttujaan
            double optData = opt3001_get_data(&i2cOPT);
            ambientLight = optData;

            // Suljetaan OPT-anturin I2C-väylä
            I2C_close(i2cOPT);
        }

        toggle = !toggle;

        // 100000 = 0,1 sekunnin viive = 10 Hz
        Task_sleep(25000 / Clock_tickPeriod); // 40 Hz

    }

}




Int main(void) {
    uint32_t patteri = HWREG(AON_BATMON_BASE + AON_BATMON_O_BAT);

    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;
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
