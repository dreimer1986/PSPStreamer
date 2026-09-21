#include <assert.h>
#include <math.h>
#include <string.h>
#include "milkdrop_warp.h"
static int powers;
float __real_powf(float, float);
float __wrap_powf(float a,float b) {powers++;return __real_powf(a,b);}
int main(void) {
    static MdVertex expected[MD_MESH_VERTICES],actual[MD_MESH_VERTICES];
    static MdPreset points[MD_GRID_POINTS];
    MdPreset p=md_presets[0];p.zoom=1;p.zoomexp=1;
    md_warp_mesh(expected,&p,17);
    for(int i=0;i<MD_GRID_POINTS;i++) {
        points[i]=p;points[i].zoomexp=i%2?100:.01f;
    }
    powers=0;md_warp_mesh_varying(actual,&p,points,17);
    assert(!powers);
    assert(!memcmp(expected,actual,sizeof(actual)));
    p.zoomexp=100;powers=0;md_warp_mesh(actual,&p,17);
    assert(!powers && !memcmp(expected,actual,sizeof(actual)));
    p.zoom=1.025f;p.zoomexp=2;powers=0;
    md_warp_mesh(actual,&p,17);
    assert(powers==2*MD_GRID_POINTS);
    for(int i=0;i<MD_MESH_VERTICES;i++)assert(isfinite(actual[i].u)&&isfinite(actual[i].v));
    return 0;
}
