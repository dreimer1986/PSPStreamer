#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tunnel_visual.h"
static struct {uint32_t before;MdVertex v[TUNNEL_VERTICES];uint32_t after;} mesh;
int main(void) {
    uint32_t texture[64*64],copy[64*64];
    tunnel_texture(texture);tunnel_texture(copy);assert(!memcmp(texture,copy,sizeof(texture)));
    for(int i=0;i<64*64;i++)assert(texture[i]>>24==255);
    assert(texture[0]!=texture[13]);
    TunnelState state={0};unsigned char bands[12]={0};
    mesh.before=0x12345678;mesh.after=0xabcdef12;
    assert(!tunnel_mesh(&state,mesh.v,TUNNEL_VERTICES-1,bands,0,1000000));
    for(int f=0;f<800;f++) {
        memset(bands,f%2?100:0,sizeof(bands));
        assert(tunnel_mesh(&state,mesh.v,TUNNEL_VERTICES,bands,f%101,1000000ULL+f*100000ULL)==TUNNEL_VERTICES);
        for(int i=0;i<TUNNEL_VERTICES;i++) {
            MdVertex *p=mesh.v+i;
            assert(isfinite(p->x) && isfinite(p->y) && isfinite(p->z));
            assert(fabsf(p->x)<15 && fabsf(p->y)<15 && p->z>-29 && p->z<1);
            assert(p->u>=0 && p->u<=1 && isfinite(p->v) && p->color>>24==255);
        }
    }
    assert(mesh.before==0x12345678 && mesh.after==0xabcdef12);
    float before=state.travel;
    tunnel_mesh(&state,mesh.v,TUNNEL_VERTICES,bands,100,100000000000ULL);
    float delta=state.travel-before;if(delta<0)delta+=256;
    assert(delta<=.501f); /* Bounded motion after a long scheduling gap. */
    TunnelState quiet={0},loud={0};
    for(int f=0;f<30;f++) {
        memset(bands,0,sizeof(bands));tunnel_mesh(&quiet,mesh.v,TUNNEL_VERTICES,bands,0,1000000+f*50000);
        memset(bands,100,sizeof(bands));tunnel_mesh(&loud,mesh.v,TUNNEL_VERTICES,bands,100,1000000+f*50000);
    }
    assert(loud.travel>quiet.travel && loud.bass>.9f && quiet.bass==0);
    puts("Tunnel: bounded finite mesh, deterministic original texture, music response and long-gap handling OK");
}
