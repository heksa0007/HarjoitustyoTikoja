#include <stdio.h>
#include <string.h>
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include "Board.h"
#include "sensors/mpu9250.h"
#include <math.h>

#define STACKSIZE 2048
Char taskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];

// MPU power pin global variables
static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;

// MPU power pin configuration
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

// MPU I2C configuration
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};

// kiihtyvyyden ja gyroskooppien globaalit muuttujat (float-tyyppisenä)
float acl_x = 0.0;
float acl_y = 0.0;
float acl_z = 0.0;  // Lisätty z-akselille
float gyro_x = 0.0;
float gyro_y = 0.0;
float gyro_z = 0.0;

// UART- ja I2C-kahvat
UART_Handle uart;
I2C_Handle i2cMPU;

// Funktio yksittäisen symbolin lähettämiseen UARTin kautta
void sendToUART(const char* symbol) {
    char message[5];
    sprintf(message, "%s\r\n\0", symbol);  // Muodostaa viestin, joka sisältää 4 tavua
    UART_write(uart, message, strlen(message) + 1);  // Lähetetään 4 tavua (mukaan lukien \0)
    Task_sleep(100000 / Clock_tickPeriod);  // Pieni viive viestien välillä
}

// Sensorin lukufunktio
Void sensorFxn(UArg arg0, UArg arg1) {
    float ax, ay, az, gx, gy, gz;
    I2C_Params i2cMPUParams;

    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;

    // MPU power on
    PIN_setOutputValue(hMpuPin, Board_MPU_POWER, Board_MPU_POWER_ON);

    // Odotetaan, että MPU-anturi käynnistyy
    Task_sleep(100000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();

    // MPU:n I2C-väylän avaus
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
        System_abort("Error Initializing I2C for MPU9250\n");
    }

    // MPU:n asetukset ja kalibrointi
    mpu9250_setup(&i2cMPU);
    System_printf("MPU9250: Setup and calibration OK\n");
    System_flush();

    // Pääsilmukka, joka lukee kiihtyvyys- ja gyroskooppiarvoja
    while (1) {
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

        // Kiihtyvyys ja gyroskooppirajojen tarkistus, ja lähetetään symbolit UARTin kautta
        if (fabs(acl_x) > 80 && acl_z < -97 && acl_z > -105) {
            // Lähetetään kirjain "S" (Morse: "...")
                sendToUART(".");
                sendToUART(".");
                sendToUART(".");

                // Välilyönti kirjainten väliin
                sendToUART(" ");

                // Lähetetään kirjain "O" (Morse: "---")
                sendToUART("-");
                sendToUART("-");
                sendToUART("-");

                // Välilyönti kirjainten väliin
                sendToUART(" ");

                // Lähetetään kirjain "S" (Morse: "...")
                sendToUART(".");
                sendToUART(".");
                sendToUART(".");

                // Viestin loppu - kolme välilyöntiä
                sendToUART(" ");
                sendToUART(" ");
                sendToUART(" ");
            Task_sleep(1000000 / Clock_tickPeriod);
        }
        if (fabs(acl_y) > 80 && acl_z < -97 && acl_z > -105) {
            // Lähetetään sana "aasi" Morse-koodattuna
            sendToUART(".");  // a: .-
            sendToUART("-");
            sendToUART(" ");  // Merkin väli

            sendToUART(".");  // a: .-
            sendToUART("-");
            sendToUART(" ");  // Merkin väli

            sendToUART(".");  // s: ...
            sendToUART(".");
            sendToUART(".");
            sendToUART(" ");  // Merkin väli

            sendToUART(".");  // i: ..
            sendToUART(".");

            // Viestin loppu (kolme välilyöntiä)
            sendToUART(" ");
            sendToUART(" ");
            sendToUART(" ");

            Task_sleep(1000000 / Clock_tickPeriod);
        }

        if (fabs(gyro_x) > 200) {
            sendToUART(".");  // gyroskoopin x-akselin yläraja
            Task_sleep(1000000 / Clock_tickPeriod);
        }
        if (fabs(gyro_y) > 200) {
            sendToUART("-");  // gyroskoopin y-akselin yläraja
            Task_sleep(1000000 / Clock_tickPeriod);
        }
        if (fabs(gyro_z) > 200) {
            sendToUART(" ");  // gyroskoopin z-akselin yläraja
            Task_sleep(1000000 / Clock_tickPeriod);
        }

        Task_sleep(100000 / Clock_tickPeriod);  // Yleinen 0,1 sekunnin viive
    }
}

/* UARTin alustus ja "Hello World" -viestin lähettäminen */
Void uartTaskFxn(UArg arg0, UArg arg1) {
    UART_Params uartParams;
    const char helloWorld[] = "Hello World\n";

    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.readMode = UART_MODE_BLOCKING;
    uartParams.baudRate = 9600;
    uartParams.dataLength = UART_LEN_8;
    uartParams.parityType = UART_PAR_NONE;
    uartParams.stopBits = UART_STOP_ONE;

    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL) {
        System_abort("Error opening the UART");
    }

    // Lähetetään alkuviesti UARTin kautta
    UART_write(uart, helloWorld, strlen(helloWorld));
}

Int main(void) {
    Task_Handle task;
    Task_Params taskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;

    // Alustetaan lauta
    Board_initGeneral();
    Board_initI2C();
    Board_initUART();

    // MPU:n virtapin
    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL) {
        System_abort("Pin open failed!");
    }

    // Luo tehtävä sensorin lukemiseen
    Task_Params_init(&taskParams);
    taskParams.stackSize = STACKSIZE;
    taskParams.stack = &taskStack;
    task = Task_create((Task_FuncPtr)sensorFxn, &taskParams, NULL);
    if (task == NULL) {
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

