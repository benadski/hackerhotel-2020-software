/*
 * text_adv.c
 *
 * Created: 06/01/2020 18:26:44
 *  Author: Badge.team
 */ 

#include <text_adv.h>           //Text adventure stuff
#include <main_def.h>
#include <resources.h>
#include <I2C.h>                //Fixed a semi-crappy lib found on internet, replace with interrupt driven one? If so, check hardware errata pdf!

//Keys are md5 hash of 'H@ck3r H0t3l 2020', split in two
const uint8_t xor_key[2][KEY_LENGTH] = {{0x74, 0xbf, 0xfa, 0x54, 0x1c, 0x96, 0xb2, 0x26},{0x1e, 0xeb, 0xd6, 0x8b, 0xc0, 0xc2, 0x0a, 0x61}};
//const unsigned char boiler_plate[]   = "Hacker Hotel 2020 by badge.team "; // boiler_plate must by 32 bytes long, excluding string_end(0)
#define L_BOILER    32

//Serial Tx string glue and send stuff
#define  TXLISTLEN  8
uint16_t txAddrList[TXLISTLEN]  = {0};      //List of external EEPROM addresses of first element of strings to be glued together
uint8_t txStrLen[TXLISTLEN]     = {0};      //List of lengths of strings to be glued together
uint8_t txAddrNow = 0;                      //Number of the string that currently is being sent
uint8_t sendPrompt = 0;
uint8_t txTypeNow = GAME;                   //Type of data that is being sent.
uint8_t txBuffer[TXLEN];                    //Buffer for string data

static object_model_t currObj;                      //Object data for current posistion in game
static uint8_t currDepth = 0xff;                    //Depth of position in game, 0xff indicates game data is not loaded from eeprom yet.
static uint16_t route[MAX_OBJ_DEPTH] = {0};         //
static uint16_t reactStr[3][32] = {{0},{0},{0}};    //
static uint8_t responseList = 0;                    //
static char specialInput[INP_LENGTH] = {0};         //Sometimes a special input is requested by the game. When this is set, input is compared to this string first.
static uint8_t specialPassed = 0;                   //If user input matches specialInput, this is set.

//Decrypts data read from I2C EEPROM, max 255 bytes at a time
void DecryptData(uint16_t offset, uint8_t length, uint8_t type, uint8_t *data){
    //offset += L_BOILER;
    while(length){
        *data ^= xor_key[type][(uint8_t)(offset%KEY_LENGTH)];
        ++data;
        ++offset;
        --length;
    }
}

//Un-flips the extra "encrypted" data for answers
void UnflipData(uint8_t length, uint8_t *data){
    for (uint8_t x = 0; x<length; ++x){
        data[x] = (data[x]<<4)|(data[x]>>4);
        data[x] ^= 0x55; 
    }
}

//Game data: Read a number of bytes and decrypt
uint8_t ExtEERead(uint16_t offset, uint8_t length, uint8_t type, uint8_t *data){
    offset &=EXT_EE_MAX;
    uint8_t reg[2] = {(uint8_t)(offset>>8), (uint8_t)(offset&0xff)};
    uint8_t error = (I2C_read_bytes(EE_I2C_ADDR, &reg[0], 2, data, length));
    if (error) return error;
    DecryptData(offset, length, type, data);
    return 0;
}

//Empty all other strings
void ClearTxAfter(uint8_t nr){
    for (uint8_t x=(nr+1); x<TXLISTLEN; ++x) txStrLen[x]=0;
}

//Check if an input string starts with compare string
uint8_t StartsWith(uint8_t *data, char *compare){
    uint8_t x=0;
    while (data[x] && compare[x]){
        if(data[x] != compare[x]) return 0;
        ++x;
    }
    if (compare[x] > 0) return 0; //Not the entire string found
    return 1;
}

//Put the addresses of text that has to be sent in the address/length lists.
uint8_t PrepareSending(uint16_t address, uint16_t length, uint8_t type){
    uint8_t x=0;
    if (length){
        
        while (length>255) {
            txAddrList[x] = address;
            txStrLen[x] = 255;
            address += 255;
            length -= 255;
            ++x;
            if (x==(TXLISTLEN-1)) return 1;
        }
        txAddrList[x]=address;
        txStrLen[x]=length%255;
        if (length>255) return 1; 
        txTypeNow = type;
    } else {
        txAddrList[0] = 0;
    }

    //Get rid of old data from previous run
    ClearTxAfter(x);

    txAddrNow = 0;  //Start at first pointer, initiates sending in CheckSend
    return 0;
}

//
void SetResponse(uint8_t number, uint16_t address, uint16_t length, uint8_t type){
    reactStr[0][number]=address;
    reactStr[1][number]=length;
    reactStr[2][number]=type;
}

//
uint8_t SetStandardResponse(uint8_t custStrEnd){

    SetResponse(custStrEnd++, A_LF, 2, TEASER);
    SetResponse(custStrEnd++, A_LOCATION, L_LOCATION, TEASER);
    //SetResponse(custStrEnd++, A_SPACE, L_SPACE, TEASER);
    reactStr[0][custStrEnd++] = CURR_LOC;
    SetResponse(custStrEnd++, A_LF, 2, TEASER);
    SetResponse(custStrEnd++, A_PROMPT, L_PROMPT, TEASER);
    //SetResponse(custStrEnd++, A_SPACE, L_SPACE, TEASER);

    return custStrEnd;
}

//Get all the relevant data and string addresses of an object
void PopulateObject(uint16_t offset, object_model_t *object){
    uint16_t parStr;
    offset += L_BOILER;

    //Fill things with fixed distance to offset
    uint8_t data[OFF_STRINGFLDS];
    ExtEERead(offset, OFF_STRINGFLDS, GAME, &data[0]);
    object->addrNextObj = (data[OFF_NEXTOBJ]<<8|data[OFF_NEXTOBJ+1])+L_BOILER;
    object->addrNextLvl = (data[OFF_NEXTLVL]<<8|data[OFF_NEXTLVL+1])+L_BOILER;
    for (uint8_t x=0; x<BYTE_FIELDS_LEN; ++x){
        object->byteField[x]=data[x+OFF_BYTEFLDS];
    }

    //Find out where all of the strings begin and how long they are
    offset += OFF_STRINGFLDS;
    for(uint8_t x=0; x<STRING_FIELDS_LEN; ++x){
        //Determine length
        ExtEERead(offset, 3, GAME, &data[0]);
        parStr = (data[0]<<8|data[1]);
        if (x >= OPEN_ACL_MSG){
            object->lenStr[x]= parStr-1;
            object->effect[x-OPEN_ACL_MSG] = data[2];
        } else {
            object->lenStr[x]= parStr;
        }
        //Determine string start location and add length to offset for next field
        offset += 2;
        object->addrStr[x]=offset;
        offset += parStr;
    }    
}

//Update game state: num -> vBBBBbbb v=value(0 is set!), BBBB=Byte number, bbb=bit number
void UpdateState(uint8_t num){
    uint8_t clearBit = num & 0x80;
    num &= 0x7f;
    if (clearBit) {
        WriteStatusBit(num, 0);
    } else {
        WriteStatusBit(num, 1);
    }
}

//Checks if state of bit BBBBbbb matches with v (inverted) bit
uint8_t CheckState(uint8_t num){
    uint8_t bitSet = 0;
    if (ReadStatusBit(num & 0x7f)){
        bitSet = 1;
    }
    if (((num & 0x80)>0)^(bitSet>0)){
        return 1;
    }
    return 0;
}

//Check if the entered letter corresponds with a name
uint8_t CheckLetter(uint16_t object, uint8_t letter){
    
    uint8_t found = 0;
    uint8_t data[32];

    object += L_BOILER;
    ExtEERead(object+OFF_STRINGFLDS, 2, GAME, &data[0]);
    uint8_t x = data[1]; //Assuming a name is not longer than 255 characters.

    while (x){
        uint8_t max;
        if (x>32) max = 32; else max = x;
        ExtEERead(object+OFF_STRINGFLDS+2, max, GAME, &data[0]);
        for (uint8_t y=0; y<max; ++y){
            if (found){
                if ((data[y]|0x20) == letter) return 1; else return 0;
            }
            if (data[y] == '[') found = 1;
        }
        object += max;
        x -= max;
    }
    return 0;
}

//Returns the child's address if the child is visible and the search letter matches
uint16_t FindChild(uint16_t parent, uint8_t letter, uint16_t start){
    
    uint16_t child = parent;
    uint8_t data[4];

    ExtEERead(child+L_BOILER, 4, GAME, &data[0]);
    parent = (data[0]<<8|data[1]);    //Next object on parent level
    child =  (data[2]<<8|data[3]);    //First object on child level

    //As long as the child is within the parent's range
    while (parent>child){

        //If child's address is higher than the start address, perform search for letter
        if (child>start){
            ExtEERead(child+OFF_BYTEFLDS+VISIBLE_ACL+L_BOILER, 1, GAME, &data[0]);
            if ((data[0] == 0)||(CheckState(data[0]))) {
                if ((letter == 0)||(CheckLetter(child, letter))) return child;
            }
        }

        //Not visible or name not right
        ExtEERead(child+L_BOILER, 2, GAME, &data[0]);
        child = (data[0]<<8|data[1]);    //Next object on child level
        
    } return 0;
}

//Allow only a(A) to z(Z) and 0 to 9 as input
uint8_t InpOkChk(uint8_t test){
    test |= 0x20;
    if ((test>='a')&&(test<='z')) return 1;
    if ((test>='0')&&(test<='9')) return 1;
    return 0;
}

//Cleans input of garbage and returns length of cleaned input "o->d2  !\0" becomes "od2\0"
uint8_t CleanInput(uint8_t *data){
    uint8_t cnt = 0;
    for (uint8_t x=0; data[x]!=0; ++x){
        data[cnt] = data[x];
        if (InpOkChk(data[x])) ++cnt;
    }
    data[cnt] = 0;
    return cnt;
}

//Send routine, optimized for low memory usage
uint8_t CheckSend(){
    static uint8_t txPart;
    uint8_t EEreadLength=0;

    //Check if more string(part)s have to be sent to the serial output if previous send operation is completed
    if ((txAddrNow < TXLISTLEN) && serTxDone){
        if (txStrLen[txAddrNow] == 0){
            txPart = 0;
            txAddrNow = TXLISTLEN;
        } else if (txPart < txStrLen[txAddrNow]){
            EEreadLength = txStrLen[txAddrNow]-txPart;
            if (EEreadLength>=TXLEN) EEreadLength = TXLEN-1;
            ExtEERead(txAddrList[txAddrNow]+txPart, EEreadLength, txTypeNow, &txBuffer[0]);
            txPart += EEreadLength;
            txBuffer[EEreadLength] = 0; //Add string terminator after piece to send to plug memory leak
            if ((txBuffer[0] == 0) && (EEreadLength)) txBuffer[0] = 0xDB; //Block character when data wrong.
            SerSend(&txBuffer[0]);
        } else {
            txPart = 0;
            ++txAddrNow;
        }
    } else if (serTxDone) return 0; //All is sent!
    return 1; //Still sending, do not change the data in the tx variables
}

//Send another part of the response and play the effect afterwards
uint8_t CheckResponse(){
    static uint8_t number = 0;
    if (responseList){
        --responseList;
        if (reactStr[0][number] == CURR_LOC) {
            PrepareSending(currObj.addrStr[NAME], currObj.lenStr[NAME], GAME);
        } else {
            PrepareSending(reactStr[0][number], reactStr[1][number], reactStr[2][number]);
        }
        ++number;

        if (responseList == 0) {
            effect = currObj.byteField[EFFECTS];
            RXCNT = 0;
            serRxDone = 0;
            number = 0;
            return 0;
        }
        return 1;
    }
    return 0;
}

//User input check (and validation)
uint8_t CheckInput(uint8_t *data){
    
    //Load game data after reboot
    if (currDepth == 0xff) {
        //Load things from EEPROM
        EERead(0, &gameState[0], BOOTCHK);   //Load game status bits from EEPROM

        uint8_t idSet = 0;
        for (uint8_t x=0; x<4; ++x){
            idSet += ReadStatusBit(110+x);
        }

        //Check if badge is reset(0 = cheated!) or new(3) or error(2)
        if (idSet != 1) {
            Reset();
        } else getID();

        inventory[0] = (gameState[INVADDR]<<8|gameState[INVADDR+1]);
        inventory[1] = (gameState[INVADDR+2]<<8|gameState[INVADDR+3]);
        SaveGameState();

        //Start at first location
        PopulateObject(route[0], &currObj);
        currDepth = 0;

        //Play an effect if configured
        if ((effect < 0x0100) && (effect ^ currObj.byteField[EFFECTS])){
            effect = currObj.byteField[EFFECTS];
            auStart = ((effect&0xE0)>0);
        }
    }

    if (serRxDone){

        //Special input requested from user by game
        if (specialInput[0]){
            specialPassed = 0;
            data[0] = 'a';
            
            //Normal code challenge
            if (StartsWith((uint8_t *)&serRx[0], &specialInput[0])) {
                specialPassed = 1;
                //specialInput[0] = 0;
                //data[1] = 0;

            //Special challenge 1
            } else if ((specialInput[0] == '1')&&(specialInput[2] == 0)) {
                uint8_t inputLen = CleanInput((uint8_t *)&serRx[0]);
                specialPassed = 2;
                data[1] = 0;

                if (inputLen >= 2) {
                    if ((serRx[0] == '0')||(serRx[0] == '1')||(serRx[0] == '2')||(serRx[0] == '3')) {
                        serRx[1] |= 0x20;
                        if ((serRx[1] == 'a')||(serRx[1] == 'e')||(serRx[1] == 'f')||(serRx[1] == 'w')) {
                            data[1] = specialInput[1]+0x11;
                            data[2] = serRx[0];
                            data[3] = serRx[1];
                            data[4] = 0;
                        }
                    }
                }
            }
            //Wrong answer
            //} else {
                //specialInput[0] = 0;
                //data[1] = 0;
            //}
        
        //Normal input
        } else {
            //Read up to the \0 character and convert to lower case.
            for (uint8_t x=0; x<RXLEN; ++x){
                if ((serRx[x]<'A')||(serRx[x]>'Z')) data[x]=serRx[x]; else data[x]=serRx[x]|0x20;
                if (serRx[x] == 0) {
                    data[x] = 0;
                    break;
                }
            }

            //No text
            if (serRx[0] == 0){
                data[0] = 0;
                RXCNT = 0;
                serRxDone = 0;
                return 1;
            }

            //Help text
            if ((data[0] == '?')||(data[0] == 'h')){
                SetResponse(0, A_HELP, L_HELP, TEASER);
                responseList = SetStandardResponse(1);
                return 1;
            }        
        
            //Alphabet text
            if (data[0] == 'a'){
                SetResponse(0, A_ALPHABET, L_ALPHABET, TEASER);
                responseList = SetStandardResponse(1);
                return 1;
            }

            //Whoami text
            if (data[0] == 'w'){
                SetResponse(0, A_HELLO, L_HELLO, TEASER);
                if (whoami == 1) {
                    SetResponse(1, A_ANUBIS, L_ANUBIS, TEASER);
                } else if (whoami == 2) {
                    SetResponse(1, A_BES, L_BES, TEASER);
                } else if (whoami == 3) {
                    SetResponse(1, A_KHONSU, L_KHONSU, TEASER);
                } else if (whoami == 4) {
                    SetResponse(1, A_THOTH, L_THOTH, TEASER);
                } else {
                    SetResponse(1, A_ERROR, L_ERROR, TEASER);
                }
                SetResponse(2, A_PLEASED, L_PLEASED, TEASER);
                responseList = SetStandardResponse(3);
                return 1;
            }

            //Quit text
            if (data[0] == 'q'){
                SetResponse(0, A_QUIT, L_QUIT, TEASER);
                responseList = SetStandardResponse(1);
                return 1;
            }

            //Change status bit
            if (data[0] == '#'){
                uint8_t bitNr = 0;
                for (uint8_t x=1; x<4; ++x) {
                    data[x] -= '0';
                    bitNr *= 10;
                    if (data[x] < 10) {
                        bitNr += data[x];
                        continue;
                    }
                    bitNr = 0;
                    break;
                }
                if (bitNr) UpdateState(bitNr);
                responseList = SetStandardResponse(0);
                return 1;
            }

            //Cheat = reset badge!
            if (StartsWith(&data[0], "iddqd")){
            
                //Reset game data by wiping the UUID bits
                for (uint8_t x=0; x<4; ++x){
                    WriteStatusBit(110+x, 0);
                }
                SaveGameState();

                uint8_t cheat[] = "Cheater! ";
                SerSpeed(60);
                while(1){
                    if (serTxDone) SerSend(&cheat[0]);
                }
            }
        } 
        //Data received, but not any of the commands above
        return 0;
    }

    //Serial input not available yet
    return 1;
}

//The game logic!
uint8_t ProcessInput(uint8_t *data){
    static object_model_t actObj1, actObj2;
    uint8_t elements = 0;

    CleanInput(&data[0]);
    uint8_t inputLen = CleanInput(&data[0]);

    if (inputLen) {

        //eXit to previous location
        if (data[0] == 'x'){

            //Standing in the Lobby?
            if ((route[currDepth] == 0)||(currDepth == 0)){
                SetResponse(elements++, A_NOTPOSSIBLE, L_NOTPOSSIBLE, TEASER);
            
            //Is there a way to go back?
            } else if (CheckState(currObj.byteField[OPEN_ACL])){
                --currDepth;
                PopulateObject(route[currDepth], &currObj);
            
            //No way out, print denied message of location
            } else {
                SetResponse(elements++, currObj.addrStr[OPEN_ACL_MSG], currObj.lenStr[OPEN_ACL_MSG], GAME);               
            }   
        
        //Enter locations or Open objects    
        } else if ((data[0] == 'e')||(data[0] == 'o')) {
                
            //Not possible, too many/little characters
            if (inputLen != 2){
                SetResponse(elements++, A_NOTPOSSIBLE, L_NOTPOSSIBLE, TEASER);
            } else {
                uint8_t canDo = 0;
                route[currDepth+1] = FindChild(route[currDepth], data[1], 0);
                    
                //Child found?
                if (route[currDepth+1]) {
                    PopulateObject(route[currDepth+1], &actObj1);
                    canDo = 1;
                //No child, maybe a step back, letter ok?
                } else if (currDepth) {
                    if (CheckLetter(route[currDepth-1], data[1])) {
                        PopulateObject(route[currDepth-1], &actObj1);
                        canDo = 1; 
                    }
                }

                //The candidate is found! Let's check if the action is legit
                if (canDo) {
                    if ((data[0] == 'e') && ((actObj1.byteField[ACTION_MASK]&ENTER)==0)) {
                        SetResponse(elements++, A_CANTENTER, L_CANTENTER, TEASER);
                    } else if ((data[0] == 'o') && ((actObj1.byteField[ACTION_MASK]&OPEN)==0)) {
                        SetResponse(elements++, A_CANTOPEN, L_CANTOPEN, TEASER);
                    
                    //Action legit, permission granted?
                    } else if (CheckState(actObj1.byteField[OPEN_ACL])) {
                            
                        //Yes! Check if we must move forward or backwards.
                        if (route[currDepth+1]) ++currDepth; else --currDepth;
                        PopulateObject(route[currDepth], &currObj);
                                                  
                    //Not granted!
                    } else {
                        route[currDepth+1] = 0;
                        SetResponse(elements++, actObj1.addrStr[OPEN_ACL_MSG], actObj1.lenStr[OPEN_ACL_MSG], GAME);                
                    }

                //No candidate
                } else {
                    SetResponse(elements++, A_DONTSEE, L_DONTSEE, TEASER);                
                }
            }

        //Look around or at objects
        } else if (data[0] == 'l') {
            if (inputLen == 1) {

                //Show info about this area first
                SetResponse(elements++, currObj.addrStr[DESC], currObj.lenStr[DESC],GAME);
                SetResponse(elements++, A_LF, 2, TEASER);
                SetResponse(elements++, A_LOOK, L_LOOK, TEASER);
                //SetResponse(elements++, A_SPACE, L_SPACE, TEASER);

                //Check the visible children first
                route[currDepth+1] = 0;
                do{
                    route[currDepth+1] = FindChild(route[currDepth], 0, route[currDepth+1]);
                    if (route[currDepth+1]) {
                        if ((route[currDepth+1] != inventory[0])&&(route[currDepth+1] != inventory[1])) {
                            PopulateObject(route[currDepth+1], &actObj1);
                            SetResponse(elements++, actObj1.addrStr[NAME], actObj1.lenStr[NAME],GAME);
                            SetResponse(elements++, A_COMMA, L_COMMA, TEASER);
                            //SetResponse(elements++, A_SPACE, L_SPACE, TEASER);
                        }
                    }
                }while (route[currDepth+1]);

                //Look back if not on level 0
                if (currDepth) {
                    PopulateObject(route[currDepth-1], &actObj1);
                    SetResponse(elements++, actObj1.addrStr[NAME], actObj1.lenStr[NAME],GAME);
                } else elements-=1;

            } else {
                route[currDepth+1] = FindChild(route[currDepth], data[1], 0);
                if (route[currDepth+1]) {
                    PopulateObject(route[currDepth+1], &actObj1);
                    SetResponse(elements++, actObj1.addrStr[DESC], actObj1.lenStr[DESC],GAME);   
                } else if (currDepth) {
                    if (CheckLetter(route[currDepth-1], data[1])) {
                        PopulateObject(route[currDepth-1], &actObj1);
                        SetResponse(elements++, actObj1.addrStr[DESC], actObj1.lenStr[DESC],GAME);
                    }
                } else {
                    SetResponse(elements++, A_DONTSEE, L_DONTSEE, TEASER);
                }
            }
        
        //Pick up an object
        } else if (data[0] == 'p') {
            if (inventory[0]&&inventory[1]) {
                SetResponse(elements++, A_CARRYTWO, L_CARRYTWO, TEASER);
            } else if (inputLen != 2) {
                SetResponse(elements++, A_NOTPOSSIBLE, L_NOTPOSSIBLE, TEASER);
            } else {
                route[currDepth+1] = FindChild(route[currDepth], data[1], route[currDepth+1]);
                if (route[currDepth+1]) {
                    if ((route[currDepth+1] == inventory[0])||(route[currDepth+1] == inventory[1])) {
                        SetResponse(elements++, A_ALREADYCARRYING, L_ALREADYCARRYING, TEASER);
                        route[currDepth+1] = 0;
                    } else {
                        //Put item in the inventory if possible
                        PopulateObject(route[currDepth+1], &actObj1);                      
                        if (actObj1.byteField[ITEM_NR]) {
                            if (inventory[0]) {
                                inventory[1] = route[currDepth+1];
                            } else {
                                inventory[0] = route[currDepth+1];
                            }
                            SetResponse(elements++, A_NOWCARRING, L_NOWCARRING, TEASER);
                            //SetResponse(elements++, A_SPACE, L_SPACE, TEASER);
                            SetResponse(elements++, actObj1.addrStr[NAME], actObj1.lenStr[NAME], GAME);
                        } else {
                            SetResponse(elements++, A_NOTPOSSIBLE, L_NOTPOSSIBLE, TEASER);
                        }
                    }
                } else SetResponse(elements++, A_NOSUCHOBJECT, L_NOSUCHOBJECT, TEASER);
            }
        } else

        //Drop item if in inventory
        if (data[0] == 'd') {
            if ((inventory[0] == 0)&&(inventory[1] == 0)){
                SetResponse(elements++, A_EMPTYHANDS, L_EMPTYHANDS, TEASER);
            } else if (inputLen != 2) {
                SetResponse(elements++, A_NOTPOSSIBLE, L_NOTPOSSIBLE, TEASER);
            } else {
                for (uint8_t x=0; x<2; ++x) {
                    if (inventory[x]) {
                        if (CheckLetter(inventory[x], data[1])) {
                            PopulateObject(inventory[x], &actObj1);
                            SetResponse(elements++, A_DROPPING, L_DROPPING, TEASER);
                            SetResponse(elements++, actObj1.addrStr[NAME], actObj1.lenStr[NAME], GAME);
                            SetResponse(elements++, A_LF, 2, TEASER);
                            SetResponse(elements++, A_RETURNING, L_RETURNING, TEASER);
                            inventory[x] = 0;
                            break;
                        }
                    }
                    if (x) SetResponse(elements++, A_NOTCARRYING, L_NOTCARRYING, TEASER);
                }
            }

        //Inventory list
        } else if (data[0] == 'i') {
            if ((inventory[0] == 0)&&(inventory[1] == 0)){
                SetResponse(elements++, A_EMPTYHANDS, L_EMPTYHANDS, TEASER);
            } else {
                SetResponse(elements++, A_NOWCARRING, L_NOWCARRING, TEASER);
                //SetResponse(elements++, A_SPACE, L_SPACE, TEASER);
                
                for (uint8_t x=0; x<2; ++x) {
                    if (inventory[x]) {
                        PopulateObject(inventory[x], &actObj1);
                        SetResponse(elements++, actObj1.addrStr[NAME], actObj1.lenStr[NAME], GAME);
                        SetResponse(elements++, A_COMMA, L_COMMA, TEASER);
                        //SetResponse(elements++, A_SPACE, L_SPACE, TEASER);
                    }
                }
                elements -= 1;
            }            
        
        //Talk, use, give, read
        } else if ((data[0] == 't')||(data[0] == 'u')||(data[0] == 'g')||(data[0] == 'r')) {
            if ((inputLen<2)||(inputLen>3)) {
                SetResponse(elements++, A_NOTPOSSIBLE, L_NOTPOSSIBLE, TEASER);                
            
            //Check for visible person or object first
            } else {
                route[currDepth+1] = FindChild(route[currDepth], data[inputLen-1], 0);
                if (route[currDepth+1]) {
                    
                    //Give item to person / use item on object 
                    if ((inputLen == 3)&&((data[0] == 'u')||(data[0] == 'g'))) {
                        for (uint8_t x=0; x<2; x++) {
                            if (inventory[x]) { 
                                if (CheckLetter(inventory[x], data[1])) {
                                    PopulateObject(inventory[x], &actObj2);
                                    x = 2;
                                }
                            }
                            if (x == 1) { 
                                SetResponse(elements++, A_NOTCARRYING, L_NOTCARRYING, TEASER);
                                data[0] = 0;
                            }
                        }

                        //Both the item and person/object are found, check if action is legit
                        if (data[0]){
                            PopulateObject(route[currDepth+1], &actObj1);

                            //Special game
                            if (actObj1.lenStr[ACTION_STR1] == 1) {
                                ExtEERead(actObj1.addrStr[ACTION_STR1], 1, GAME, &data[2]);
                                if (data[2] == '1') {
                                    uint8_t item = actObj2.byteField[ITEM_NR];
                                    if ((item < 31)||(item > 34)) {
                                        SetResponse(elements++, A_CANTUSE, L_CANTUSE, TEASER);
                                    } else {
                                        SetResponse(elements++, A_PRIEST, L_PRIEST, TEASER);
                                        SetResponse(elements++, A_LF, 2, TEASER);
                                        SetResponse(elements++, A_RESPONSE, L_RESPONSE, TEASER);
                                        specialInput[0] = '1';
                                        specialInput[1] = item;
                                        specialInput[2] = 0;
                                    }
                                } else {
                                    SetResponse(elements++, A_ERROR, L_ERROR, TEASER);
                                }

                            //Normal "use ... on ..." or "give ... to ..." action
                            } else if (actObj1.byteField[ACTION_ITEM] == actObj2.byteField[ITEM_NR]) {
                                UpdateState(actObj1.byteField[ACTION_STATE]);
                                SetResponse(elements++, actObj1.addrStr[ACTION_MSG], actObj1.lenStr[ACTION_MSG], GAME);
                            } else {
                                if (data[0] == 'u') {
                                    SetResponse(elements++, A_CANTUSE, L_CANTUSE, TEASER);
                                } else if (data[0] == 'g') {
                                    SetResponse(elements++, A_CANTGIVE, L_CANTGIVE, TEASER);                                    
                                }
                            }
                        }
                    
                    //Talk to person, read or use object (without item)                          
                    } else {
                        PopulateObject(route[currDepth+1], &actObj1);
                        if ((data[0] == 't')&&((255 - actObj1.byteField[ACTION_MASK]) & TALK)) {
                            SetResponse(elements++, A_WHYTALK, L_WHYTALK, TEASER);
                            SetResponse(elements++, actObj1.addrStr[NAME], actObj1.lenStr[NAME], GAME);
                        } else if ((data[0] == 'u')&&((255-actObj1.byteField[ACTION_MASK]) & USE)) {
                            SetResponse(elements++, A_CANTUSE, L_CANTUSE, TEASER);
                        } else if ((data[0] == 'r')&&((255-actObj1.byteField[ACTION_MASK]) & READ)) {
                            SetResponse(elements++, A_CANTREAD, L_CANTREAD, TEASER);
                        } else {

                            //Special game, not enough characters
                            if (actObj1.lenStr[ACTION_STR1] == 1) {
                                ExtEERead(actObj1.addrStr[ACTION_STR1], 1, GAME, &data[2]);
                                if (data[2] == '1') {
                                    SetResponse(elements++, A_PLEASEOFFER, L_PLEASEOFFER, TEASER);
                                }

                            //General request
                            } else if (actObj1.lenStr[ACTION_STR1]) {
                                SetResponse(elements++, actObj1.addrStr[ACTION_STR1], actObj1.lenStr[ACTION_STR1], GAME);
                                SetResponse(elements++, A_LF, 2, TEASER);
                                SetResponse(elements++, A_RESPONSE, L_RESPONSE, TEASER);
                                if (actObj1.lenStr[ACTION_STR2]>(INP_LENGTH-1)) actObj1.lenStr[ACTION_STR2] = (INP_LENGTH-1);
                                ExtEERead(actObj1.addrStr[ACTION_STR2], actObj1.lenStr[ACTION_STR2], GAME, (uint8_t *)&specialInput[0]);
                                UnflipData(actObj1.lenStr[ACTION_STR2], &specialInput[0]);
                                specialInput[actObj1.lenStr[ACTION_STR2]] = 0;
                                //specialPassed = 0;
                            } else if (CheckState(actObj1.byteField[ACTION_ACL])){
                                SetResponse(elements++, actObj1.addrStr[ACTION_MSG], actObj1.lenStr[ACTION_MSG], GAME);
                                UpdateState(actObj1.byteField[ACTION_STATE]);
                            } else {
                                SetResponse(elements++, actObj1.addrStr[ACTION_ACL_MSG], actObj1.lenStr[ACTION_ACL_MSG], GAME);
                            }
                        }
                    }

                //Person or object not found
                } else {
                    if ((data[0] == 't')||(data[0] == 'g')){
                        SetResponse(elements++, A_NOSUCHPERSON, L_NOSUCHPERSON, TEASER);
                    } else {
                        SetResponse(elements++, A_NOSUCHOBJECT, L_NOSUCHOBJECT, TEASER);
                    }
                }
            }
        
        //Special answer given
        } else if (data[0] == 'a'){
            
            //Priest offerings
            if (specialPassed >= 2) {
                if (data[1] > 0) {

                    uint8_t  digit[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                    uint32_t answer = 0;
                    uint8_t  n = 0;

                    /* CALCULATION                    
                        data[x]: x=1->offering x=2->kneelings x=3->element whoami->person
                    
                        answer = ((offering  & 2) << 19) + ((offering  & 1) << 8) + \
                        ((element   & 2) << 15) + ((element   & 1) << 4) + \
                        ((kneelings & 2) << 11) + ((kneelings & 1))
                        answer = answer << (3-person)
                    */
                    data[1]-='0';
                    data[2]-='0';
                    if (data[3] == 'a') data[3] = 1;
                    else if (data[3] == 'e') data[3] = 0;
                    else if (data[3] == 'f') data[3] = 3;
                    else data[3] = 2;

                    if (data[1] & 2) answer += (1UL << 20);
                    if (data[1] & 1) answer += (1 << 8);
                    if (data[3] & 2) answer += (1UL << 16);
                    if (data[3] & 1) answer += (1 << 4);
                    if (data[2] & 2) answer += (1 << 12);
                    if (data[2] & 1) answer += 1;
                    answer <<= (3 - whoami);            

                    SetResponse(elements++, A_YOURPART, L_YOURPART, TEASER);
                    
                    //Set up sending out number
                    for (n=9; n>=0; --n) {
                        digit[n] = answer % 10;
                        answer /= 10;
                        if (answer == 0) break;
                    }
                    for (; n<10; ++n) {
                        SetResponse(elements++, A_DIGITS+digit[n], 1, TEASER);
                    }

                } else {
                    SetResponse(elements++, A_BADOFFERING, L_BADOFFERING, TEASER);
                }
            
            //Other questions    
            } else if (specialPassed == 1) {
                PopulateObject(route[currDepth+1], &actObj1);
                SetResponse(elements++, actObj1.addrStr[ACTION_MSG], actObj1.lenStr[ACTION_MSG], GAME);
                UpdateState(actObj1.byteField[ACTION_STATE]);
            } else {
                PopulateObject(route[currDepth+1], &actObj1);
                SetResponse(elements++, A_INCORRECT, L_INCORRECT, TEASER);
            }
            specialInput[0] = 0;

        //Faulty input
        } else {
        
            ;//No clue, no valid input...
                
        }
            
        //Input handled
        data[0] = 0;
        serRxDone = 0;
        RXCNT = 0;
        if (specialInput[0]) responseList = elements; else responseList = SetStandardResponse(elements);

    }
    
    return 0;
}


//MAIN GAME STRUCTURE
uint8_t TextAdventure(){
    static uint8_t serInput[RXLEN];
    
    //Still sending data to serial?
    if (CheckSend()) return 1;

    //Not sending? Process next response part to send, if any.
    if (CheckResponse()) return 1;        

    //No responses to send, check if there is user input.
    if (CheckInput(&serInput[0])) return 2; 

    //Input found, process and save (changes only)
    ProcessInput(&serInput[0]);
    SaveGameState();

    return 0;
}