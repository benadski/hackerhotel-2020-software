/*
 * lanyard.c
 *
 * Created: 02/02/2020 0:56
 * Author: Badge.team
 *
 *                 o--o--=g=--o--o
 *                /      .'       \
 *                o      '.       o
 *                 \             /
 *                  o           o
 *                   \         /
 *                    o       o
 *                     \     /
 *                      o   o
 *                       \_/
 *                        =
 *                       .^.
 *                      '   '
 *                      '. .'
 *                        !    lc
 */

#include <lanyard.h>
#include <main_def.h>
#include <resources.h>
#include <I2C.h>

//Printing of the lanyard failed, only part of the code was printed
//const uint8_t   lanyardCode[] = { 1,0,0,2, 1,2,0,1, 1,3,0,3, 1,3,1,0, 1,2,1,1, 1,3,1,0 };
const uint8_t   lanyardCode[] = { 1,0,0,2, 1,2,0,1, 1,3,0,3};

uint8_t         lanyardPos = 0;
uint8_t         lanyardCnt = 0;
uint8_t         lanyardState = TRUE;
uint16_t        lanyardLastActive = 0;


// Main game loop
uint8_t LanyardCode(){
    if (gameNow == LANYARD && idleTimeout(lanyardLastActive,LANYARD_MAX_IDLE)) {
        /* clean up maze game and go back to text game */
        gameNow = TEXT;
        effect = 0;
        return 0;
    }

    if (CheckState(LANYARD_COMPLETED))
        return 0;

    if ( gameNow != TEXT && gameNow != LANYARD )
        return 0;

    if (buttonState == 0xff)
        return 0;

    if (buttonState == lastButtonState)
        return 0;

    lanyardLastActive = getClock();

    if (lastButtonState == 0xff){
        if ((gameNow != LANYARD) || (lanyardState&GAME_OVER) ) {
            // init Lanyard game
            gameNow         = LANYARD;
            lanyardPos      = 0;
            lanyardCnt      = 0;
            lanyardState    = TRUE;
            SetHackerLeds(0,0);
        }

        if (buttonState == lanyardCode[lanyardPos]) {
            lanyardState &= TRUE;
            iLED[EYE[R][L]] = 0;
            iLED[EYE[R][R]] = 0;
        } else {
            lanyardState = FALSE;
            if (lanyardPos == 0 ) {
                gameNow         = BASTET;
                return 0;
            }
        }
        lanyardPos++;
        lanyardCnt++;            
        WingBar(lanyardCnt,lanyardCnt);
        if (lanyardCnt >= 4) {
            lanyardCnt = 0;
            if (lanyardState == TRUE) {
                if (lanyardCnt == 0) {
                    WingBar(0,0);
                    iLED[HCKR[G][(lanyardPos>>1)-2]] = dimValue;
                    iLED[HCKR[G][(lanyardPos>>1)-1]] = dimValue;
                }
                if (lanyardPos == LANYARD_LEN) {
                    UpdateState(LANYARD_COMPLETED);
                    effect    = 0x42;
                    /*TODO state = STATE_MUSIC;*/
                }
            } else {
                lanyardState |= GAME_OVER;
                effect  = 0x31;
                WingBar(0,0);
            }
        }
    }

    return 0;
}
