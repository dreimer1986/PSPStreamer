#include <assert.h>
#include <sys/random.h>
static inline unsigned int sctrlKernelRand(void) {
    unsigned int value;assert(getrandom(&value,sizeof(value),0)==sizeof(value));return value;
}
