#include <assert.h>
#include <string.h>
#include "../xbox-client/http_status.h"
int main(void) {
    const char *ok="HTTP/1.1 200 OK\r\nServer: test\r\n\r\n";
    assert(xbox_http_status(ok,strlen(ok))==200);
    for(unsigned n=0;n<13;n++)assert(xbox_http_status(ok,n)==0);
    const char *cases[]={"HTTP/1.0 401 Unauthorized\r\n", "HTTP/1.1 503 Unavailable\r\n",
        "HTTP/1.1 200\r\n", "HTTP/1.1 20x OK\r\n", "HTTP/1.1 2000 OK\r\n",
        "HTTP/2.0 200 OK\r\n", "HTTP/1.1 099 Bad\r\n", "HTTP/1.1 600 Bad\r\n"};
    int expected[]={401,503,200,0,0,0,0,0};
    for(unsigned i=0;i<sizeof(expected)/sizeof(*expected);i++)
        assert(xbox_http_status(cases[i],strlen(cases[i]))==expected[i]);
    return 0;
}
