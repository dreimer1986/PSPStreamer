/* Focused host check for the same bounded parser used by the Xbox UI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../xbox-client/catalog.h"
int main(void) {
    static char reply[512*1024];
    size_t size=fread(reply,1,sizeof(reply)-1,stdin);
    assert(size && !ferror(stdin));reply[size]=0;
    assert(parse_catalog(reply));
    printf("catalog: %d entries, total %d\n",entry_count,total_entries);
    return 0;
}
