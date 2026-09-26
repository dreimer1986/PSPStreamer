from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class SmallVisualUpdates(unittest.TestCase):
    def test_engine_faces_and_overlay_visibility(self):
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include "overlay_lifetime.h"
#include "cave_engine.h"
typedef struct {float u,v;uint32_t color;float x,y,z;} MdVertex;
#include "cave_ship_data.h"
int main(void){
    assert(oc_overlay_deadline(0,100,0,0,1)==0);
    assert(oc_overlay_deadline(0,100,1,0,1)==5000100);
    assert(oc_overlay_deadline(5000100,5000100,0,0,1)==0);
    assert(oc_overlay_deadline(5000100,200,1,0,1)==0);
    assert(oc_overlay_deadline(0,100,0,1,1)==~0ULL);
    assert(oc_overlay_deadline(~0ULL,6000000,1,1,1)==~0ULL);
    /* Cleanup clears until before calling the inactive display policy. */
    assert(oc_overlay_deadline(0,6000000,0,1,0)==0);
    int left=0,right=0;
    for(int i=0;i<CAVE_SHIP_VERTICES;i++){
        MdVertex v=cave_ship_mesh[i];
        unsigned a=cave_engine_color(v.x,v.y,v.z,v.color,0);
        unsigned b=cave_engine_color(v.x,v.y,v.z,v.color,1);
        if(a!=v.color){
            assert(v.y>0 && v.z>.3f && fabsf(v.x)<.112f);
            assert(a!=b && (a&255)==155 && (b&255)==255 && (b>>24)==255);
            assert(((b>>8)&255)>((a>>8)&255)+140);
            assert(((b>>16)&255)>((a>>16)&255)+120);
            if(v.x<0)left++;else right++;
            assert(cave_engine_color(v.x,v.y,v.z,v.color,NAN)==a);
        }else assert(b==v.color);
    }
    assert(left>0 && left==right);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as temp:
            binary=Path(temp)/'visual'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=undefined','-I',str(ROOT/'psp-client'),'-I',str(ROOT/'psp-overclock'),
                '-o',str(binary)],input=code,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=3)
