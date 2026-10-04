#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
static char host[64]="192.168.1.1",password[129]="old";
static unsigned port=8091;static int output_height=480;
static int MoveFileA(const char *a,const char *b){return rename(a,b)==0;}
#include "../xbox-client/server_settings.h"
int main(void){
    strcpy(server_draft_host,"192.168.1.42");strcpy(server_draft_port,"8091");strcpy(server_draft_password,"new secret");
    assert(server_draft_valid());assert(server_save());assert(connection_ready);
    assert(!strcmp(password,"new secret"));assert(port==8091);
    strcpy(server_draft_port,"65536");assert(!server_draft_valid());assert(!server_save());
    strcpy(server_draft_port,"80x");assert(!server_draft_valid());
    strcpy(server_draft_port,"80");strcpy(server_draft_host,"https://wrong");assert(!server_draft_valid());
    keyboard_begin(2);keyboard_key='A'-32;keyboard_insert();keyboard_accept();assert(!strcmp(server_draft_password,"new secretA"));
    assert(!*keyboard_draft);keyboard_begin(2);keyboard_key=95;keyboard_insert();keyboard_accept();assert(strstr(server_draft_password,"ä"));
    strcpy(config_old_host,"192.168.1.1");strcpy(config_old_password,"old");config_old_port=8091;config_testing=1;
    config_restore_test();assert(!config_testing&&!strcmp(password,"old")&&!strcmp(host,"192.168.1.1"));
    remove("D:\\server.cfg");remove("D:\\server.bak");return 0;
}
