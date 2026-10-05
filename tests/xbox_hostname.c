#include <assert.h>
#include "../xbox-client/hostname.h"
int main(void){
    assert(xbox_hostname_valid("pspstream.example.org"));
    assert(xbox_hostname_valid("192.168.181.42"));
    assert(xbox_hostname_valid("xn--bro-hoa.example"));
    assert(!xbox_hostname_valid("https://example.org"));
    assert(!xbox_hostname_valid("example.org\r\nHeader: x"));
    assert(!xbox_hostname_valid("-host.example"));
    assert(!xbox_hostname_valid("host-.example"));
    assert(!xbox_hostname_valid("a..b"));assert(!xbox_hostname_valid(""));
    return 0;
}
