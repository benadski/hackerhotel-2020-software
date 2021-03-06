/*
 * resources.h
 *
 * Contains all the crap that is needed under the hood 
 *
 * Created: 06/01/2020 18:25:19
 *  Author: Badge.team
 */ 

#ifndef RESOURCES_H_
#define RESOURCES_H_
    #include <main_def.h>
    #include <avr/io.h>
    #include <avr/interrupt.h>
    #include <stdlib.h>

//    #define PURIST_BADGE        1337

    #define FIRST_SUMMER        117
    #define SUMMERS_COMPLETED   118
    #define HACKER_STATES       122
    #define GEM_STATE           64

    void Setup();
    /*
    ISR(TCA0_LUNF_vect);                                                // LED matrix interrupt routine
    ISR(TCB0_INT_vect);                                                 // Used for sending serial data and serSpeed "typing" effect
    ISR(TCB1_INT_vect);
    ISR(USART0_RXC_vect);                                               // RX handling of serial data
    ISR(USART0_DRE_vect);                                               // TX handling of serial data, working together with TCB0_INT_vect
    ISR(ADC0_RESRDY_vect);                                              // Used for getting temperature (adcTemp) and audio input data
    ISR(ADC1_RESRDY_vect);                                              // Used for getting light (adcPhot), magnetic (adcHall) and raw button values (adcButt)
    ISR(RTC_PIT_vect);                                                  // Periodic interrupt for triggering button and sensor readout of ADC1 (32 samples per second)
    */

    uint8_t SerSend(unsigned char *addr);                               // Send characters beginning with *addr, stops at string_end (0 character).
    void SerSpeed(uint8_t serSpd);                                      // Change the character speed from badge to user for "typing" effects during the text adventure.

    void SelectTSens();                                                 // Select the temperature sensor for ADC0 (discard the first two samples after switching)
    void SelectAuIn();                                                  // Select the "audio input" for ADC0 (discard the first two samples after switching)

    uint8_t CheckButtons();                        // Readout of the button state, with duration. Run periodically, but not faster than (PIT interrupt sps / 3) times per second.

    void EERead(uint8_t eeAddr, uint8_t *eeValues, uint8_t size);       // Read from internal EEPROM, wraps around if eeAddr+size>255
    uint8_t EEWrite(uint8_t eeAddr, uint8_t *eeValues, uint8_t size);   // Write to internal EEPROM, wraps around if eeAddr+size>255
    uint8_t ExtEERead(uint16_t offset, uint8_t length, uint8_t type, uint8_t *data);

    uint8_t lfsr();
    void floatSpeed(uint8_t bits, uint16_t min, uint16_t max);
    uint8_t floatAround(uint8_t sample, uint8_t bits, uint8_t min, uint8_t max);

    void LoadGameState();
    uint8_t SaveGameState();
    uint8_t ReadStatusBit(uint8_t number);
    void WriteStatusBit(uint8_t number, uint8_t state);
    void UpdateState(uint8_t num);
    uint8_t CheckState(uint8_t num);

    uint8_t getID();
    void WipeAfterBoot(uint8_t full);
    void Reset();

    uint8_t HotSummer();

    void VictoryDance();
    void SetHackerLeds(uint8_t r, uint8_t g);
    void SetBothEyes(uint8_t r, uint8_t g);
    void WingBar(int8_t l, int8_t r);
    void GenerateBlinks();
    uint8_t GenerateAudio();

    uint16_t getClock();                                                // get actual time in seconds to use for idle timeouts
    uint8_t idleTimeout(uint16_t lastActive, uint16_t maxIdle);         // check if idle for too long

    uint8_t SelfTest();

#endif /* RESOURCES_H_ */
