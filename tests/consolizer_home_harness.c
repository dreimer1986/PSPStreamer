#include <assert.h>
#define PSP_CTRL_SELECT 1U
#define PSP_CTRL_START 8U
#define PSP_CTRL_HOME 0x10000U
#include "psp-controller/home_button.h"
int main(void) {
    PadHome state={0};unsigned buttons=9;
    assert(!pad_home_button(&state,&buttons,0,1) && !buttons);
    buttons=9;assert(!pad_home_button(&state,&buttons,999999,1));
    buttons=9;assert(pad_home_button(&state,&buttons,1000000,1)==PSP_CTRL_HOME && !buttons);
    buttons=9;assert(pad_home_button(&state,&buttons,1149999,1)==PSP_CTRL_HOME);
    buttons=9;assert(!pad_home_button(&state,&buttons,1150000,1));
    buttons=9;assert(!pad_home_button(&state,&buttons,9000000,1));
    buttons=8;assert(!pad_home_button(&state,&buttons,9000001,1) && buttons==8);
    buttons=9;assert(!pad_home_button(&state,&buttons,10000000,1));
    buttons=9;assert(pad_home_button(&state,&buttons,11000000,1)==PSP_CTRL_HOME);
    buttons=9;assert(!pad_home_button(&state,&buttons,11000001,0) && buttons==9);
    buttons=9;assert(!pad_home_button(&state,&buttons,12000000,1));
    buttons=0;assert(!pad_home_button(&state,&buttons,12100000,1));
    buttons=9;assert(!pad_home_button(&state,&buttons,14000000,1));
    return 0;
}
