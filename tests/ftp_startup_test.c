#include <assert.h>
#include "../psp-ftp/startup.h"
int main(void) {
    FtpStartup s={0};
    assert(!ftp_startup_ready(&s,20000000,0));
    assert(!ftp_startup_ready(&s,30000000,1));
    assert(!ftp_startup_ready(&s,34999999,1));
    assert(ftp_startup_ready(&s,35000000,1));
    assert(!ftp_startup_ready(&s,36000000,0));
    assert(!ftp_startup_ready(&s,37000000,1));
    assert(!ftp_startup_ready(&s,41999999,1));
    assert(ftp_startup_ready(&s,42000000,1));
    return 0;
}
