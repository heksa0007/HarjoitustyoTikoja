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

// kiihtyvyyden globaali muuttuja (double-tyyppisenä)
float acl_x = 0.0;
float acl_y = 0.0;

// UART- ja I2C-kahvat
UART_Handle uart;
I2C_Handle i2cMPU;

// Sensorin lukufunktio
Void sensorFxn(UArg arg0, UArg arg1) {
    float az, gx, gy, gz;
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

    // Pääsilmukka, joka lukee kiihtyvyysarvoja
    while (1) {
        // Haetaan data MPU-anturin avulla
        float ax, ay;
        mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);  // ax saadaan paikallisena muuttujana
        acl_x = ax * 100;  // Tallennetaan kiihdytysarvo globaaliin muuttujaan ja skaalataan
        acl_y = ay * 100;  // Tallennetaan kiihdytysarvo globaaliin muuttujaan ja skaalataan


        System_printf("%d, %d\n", (int)(ax * 100), (int)(ay * 100));
        System_flush();

        // Jos kiihtyvyys ylittää 70, lähetetään "Onnistuit" UARTin kautta
        if (acl_x > 80) {
            const char successMsg[] = ".\r\n";
            UART_write(uart, successMsg, strlen(successMsg));

            // Odotetaan ylimääräinen viive, kun ylitetään 70
                        Task_sleep(1000000 / Clock_tickPeriod);  // Sleep 1 second
        }

        // Jos kiihtyvyys ylittää 70, lähetetään "Onnistuit" UARTin kautta
                if (acl_y > 80) {
                    const char successMsg[] = "-\r\n";
                    UART_write(uart, successMsg, strlen(successMsg));

                    // Odotetaan ylimääräinen viive, kun ylitetään 70
                                Task_sleep(1000000 / Clock_tickPeriod);  // Sleep 1 second
                }
       // Jos kiihtyvyys ylittää 70, lähetetään "Onnistuit" UARTin kautta
                if (acl_x < -80) {
                    const char successMsg[] = " \r\n";
                    UART_write(uart, successMsg, strlen(successMsg));

                   // Odotetaan ylimääräinen viive, kun ylitetään 70
                                 Task_sleep(1000000 / Clock_tickPeriod);  // Sleep 1 second
                                }
      // Jos kiihtyvyys ylittää 70, lähetetään "Onnistuit" UARTin kautta
                if (acl_y < -80) {
                    const char successMsg[] = "lahetus\r\n";
                    UART_write(uart, successMsg, strlen(successMsg));

                    // Odotetaan ylimääräinen viive, kun ylitetään 70
                                Task_sleep(1000000 / Clock_tickPeriod);  // Sleep 1 second
                                                }


        Task_sleep(100000 / Clock_tickPeriod);  // Sleep 1 second
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
    uartParams.baudRate = 9600; // nopeus 9600baud
    uartParams.dataLength = UART_LEN_8; // 8
    uartParams.parityType = UART_PAR_NONE; // n
    uartParams.stopBits = UART_STOP_ONE; // 1

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

