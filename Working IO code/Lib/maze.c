/*
 * maze.c
 *
 * Created: 02/02/2020 0:56
 * Author: Badge.team
 */ 

#include <maze.h>
#include <main_def.h>
#include <resources.h>
#include <I2C.h>

uint8_t         curHallState = 0;
uint8_t         newHallState = 0;

const uint8_t   mazeCode[] = {1,1,2, 1,2,2, 1,2,2, 1,2,2, 2,2,2, 1,1,2};
uint8_t         mazePos = 0;
uint8_t         mazeHckrPos = 0;
uint8_t         mazeCnt = 0;
uint8_t         mazeState = TRUE;
uint8_t         inverted  = FALSE;
uint16_t        mazeLastActive = 0;

void initMaze() {
    mazeHckrPos = 0;
    mazePos = 0;
    mazeCnt = 0;
    mazeState = TRUE;
    inverted  = FALSE;
    iLED[CAT]       = 0;
    iLED[EYE[G][L]] = 0;
    iLED[EYE[G][R]] = 0;
    iLED[EYE[R][L]] = 0;
    iLED[EYE[R][R]] = 0;
    for (int i=0; i<6; i++ )
        iLED[HCKR[G][i]] = 0;
}

void showFieldStrength(int16_t val) {
    int16_t tmp;

    tmp = abs(val);

    if ( tmp*2 > HALL_LOW )
        gameNow = MAZE;

    if (gameNow == MAZE) {
        if ( tmp*2 < HALL_LOW )
            WingBar(0,0);
        else if ( tmp   < HALL_LOW )
            WingBar(1,1);
        else if ( tmp*2 < HALL_HIGH )
            WingBar(2,2);
        else if ( tmp*3 < HALL_HIGH*2 )
            WingBar(3,3);
        else if ( tmp   < HALL_HIGH )
            WingBar(4,4);
        else
            WingBar(5,5);
    }
}

// Main game loop
uint8_t MagnetMaze(){
    if (gameNow == MAZE && idleTimeout(mazeLastActive,MAZE_MAX_IDLE)) {
        /* clean up maze game and go back to text game */
        initMaze();
        gameNow = TEXT;
        effect = 0;
        return 0;
    }
        
    if (CheckState(MAZE_INACTIVE))
        return 0;

    if (CheckState(MAZE_COMPLETED))
        return 0;

    if ( (gameNow != TEXT) && (gameNow != MAZE) )
        return 0;

    if (calHall == 0)
        calHall = adcHall;

    int16_t valHall = adcHall - calHall;
    showFieldStrength(valHall);

    switch (curHallState) {
        case 0:
            if ( valHall + HALL_HIGH < 0 ) {
                newHallState = 1;
            } else if ( valHall - HALL_HIGH > 0 ) {
                newHallState = 2;
            } else {
                newHallState = 0;
            }
            break;
        
        case 1:
            if ( valHall - HALL_HIGH > 0 ) {
                newHallState = 2;
            } else if ( valHall + HALL_LOW > 0 ) {
                newHallState = 0;
            } else {
                newHallState = 1;
            }
            break;
        
        case 2:
            if ( valHall + HALL_HIGH < 0 ) {
                newHallState = 1;
            } else if ( valHall - HALL_LOW < 0 ) {
                newHallState = 0;
            } else {
                newHallState = 2;
            }
            break;
    }

    /* Indicate that the magnet was read succesfully */
    iLED[CAT] = (newHallState ? dimValue : 0);
    effect = 0x19f;

    if (newHallState != curHallState) {
        /* keep track of time to enable an idle timeout to exit the game */
        mazeLastActive = getClock();

        /* Make the maze work regardless of badge orientation */
        if ((mazePos == 0) && (newHallState != 0))
            inverted = (newHallState != mazeCode[0]) ? TRUE : FALSE;

        curHallState = newHallState;
        
        if (curHallState != 0) {
            gameNow = MAZE;
            if ( (inverted ? curHallState^3 : curHallState) == mazeCode[mazePos]) {
                mazeState &= TRUE;
                iLED[EYE[R][L]] = 0;
                iLED[EYE[R][R]] = 0;
            } else {
                mazeState = FALSE;
            }
            mazePos++;
            mazeCnt++;            
            if (mazeCnt >= 3) {
                mazeCnt = 0;
                if (mazeState == TRUE) {
                    iLED[HCKR[G][mazeHckrPos]] = dimValue;
                    mazeHckrPos++;
                    if (mazePos == sizeof(mazeCode)) {
                        UpdateState(MAZE_COMPLETED);
                        iLED[CAT]       = 0;
                        effect = 0x42;
                        /*TODO state = STATE_MUSIC;*/
                    }
                } else {
                    initMaze();
                    gameNow   = TEXT;
                    effect = 0x21;
                }
            }
        }
    }
    return 0;
}
