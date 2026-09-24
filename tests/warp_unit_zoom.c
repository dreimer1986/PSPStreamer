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
    /* Exact comparison against the former expanded-triangle blend. Exercise
     * independent clocks, per-pixel inputs, endpoints and differing decay. */
    static MdVertex old_mesh[MD_MESH_VERTICES],fresh_mesh[MD_MESH_VERTICES];
    static MdPreset old_points[MD_GRID_POINTS];
    for(int i=0;i<MD_GRID_POINTS;i++) {
        points[i]=md_presets[0];old_points[i]=md_presets[1];
        points[i].rotation=i*.003f;old_points[i].dx=.01f*sinf((float)i);
        points[i].zoomexp=.8f;old_points[i].zoomexp=1.2f;
    }
    for(int varying=0;varying<4;varying++)for(int step=0;step<=20;step++) {
        float weight=step/20.f;
        const MdPreset *a=varying&1?points:NULL,*b=varying&2?old_points:NULL;
        md_warp_mesh_varying(fresh_mesh,&md_presets[0],a,17.3f);
        md_warp_mesh_varying(old_mesh,&md_presets[1],b,31.7f);
        memcpy(expected,fresh_mesh,sizeof(expected));
        for(int i=0;i<MD_MESH_VERTICES;i++) {
            expected[i].u=old_mesh[i].u+(fresh_mesh[i].u-old_mesh[i].u)*weight;
            expected[i].v=old_mesh[i].v+(fresh_mesh[i].v-old_mesh[i].v)*weight;
            unsigned color=0;
            for(int c=0;c<4;c++) {
                int x=(old_mesh[i].color>>(8*c))&255,y=(fresh_mesh[i].color>>(8*c))&255;
                color|=(unsigned)(x+(y-x)*weight)<<(8*c);
            }
            expected[i].color=color;
        }
        md_warp_mesh_blended(actual,&md_presets[0],a,17.3f,&md_presets[1],b,31.7f,weight);
        assert(!memcmp(expected,actual,sizeof(expected)));
    }
    return 0;
}
