#include "Arduino.h"
#include "gen_func.h"

void resetTimer(uint32_t *timer){
    *timer=millis();
}

uint32_t getTime(uint32_t timer){
    return millis()-timer;
}