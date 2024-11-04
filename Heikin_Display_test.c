#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/mw/display/Display.h>
#include <stdio.h>
#include "Board.h"

#define STACKSIZE 2048
Char displayTaskStack[STACKSIZE];

// Task-funktio, joka vuorottelee LCD-näytön ja UARTin välillä
Void displayTaskFxn(UArg arg0, UArg arg1) {
    Display_Handle displayHandle;
    Display_Params params;

    // Alustetaan näyttöparametrit
    Display_Params_init(&params);
    params.lineClearMode = DISPLAY_CLEAR_BOTH;  // Tyhjennetään rivi tulostuksen yhteydessä

    // Yritetään avata LCD-näyttö
    displayHandle = Display_open(Display_Type_LCD, &params);
    if (displayHandle == NULL) {
        System_printf("LCD-näytön avaaminen epäonnistui.\n");
        System_flush();
    } else {
        // Tulostetaan teksti LCD-näytölle
        Display_print0(displayHandle, 0, 0, "Hello, LCD!");

        // Odotetaan 3 sekuntia ennen kuin vaihdetaan UARTiin
        Task_sleep(3000);

        // Suljetaan LCD-näyttö, jotta voimme käyttää UARTia
        Display_close(displayHandle);
    }

    // Yritetään avata UART-näyttö
    displayHandle = Display_open(Display_Type_UART, &params);
    if (displayHandle != NULL) {
        // Lähetetään viesti tietokoneen komentokehotteelle UARTin kautta
        Display_print0(displayHandle, 0, 0, "Hello from SensorTag via UART!");

        // Odotetaan 3 sekuntia ennen kuin vaihdetaan takaisin LCD:hen
        Task_sleep(3000);

        // Suljetaan UART, jotta voimme ottaa LCD:n uudelleen käyttöön
        Display_close(displayHandle);
    } else {
        System_printf("UARTin avaaminen epäonnistui.\n");
        System_flush();
    }

    // Yritetään avata LCD-näyttö uudelleen
    displayHandle = Display_open(Display_Type_LCD, &params);
    if (displayHandle != NULL) {
        Display_print0(displayHandle, 0, 0, "Back to LCD mode");
    }

    // Jätetään viestit näytölle pysyvästi
    while (1) {
        Task_sleep(1000);  // Odota 1 sekunti kerrallaan
    }
}

/* Main-funktio */
Int main(void) {
    Task_Handle displayTaskHandle;
    Task_Params displayTaskParams;

    // Alustetaan lauta
    Board_initGeneral();

    // Luodaan taski, joka hoitaa LCD-näytön ja UARTin
    Task_Params_init(&displayTaskParams);
    displayTaskParams.stackSize = STACKSIZE;
    displayTaskParams.stack = &displayTaskStack;
    displayTaskParams.priority = 2;
    displayTaskHandle = Task_create(displayTaskFxn, &displayTaskParams, NULL);
    if (displayTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    // Käynnistetään BIOS
    BIOS_start();

    return (0);
}

