/* Full shared frame submission against the bounded Xbox command recorder. */
#include <assert.h>
#include "../xbox-client/visual_gpu.c"
#include "../xbox-client/generated/visual_effects.c"
int main(int argc,char **argv){
    assert(argc==3);unsigned char bands[12]={20,30,50,70,35,50,25,30,20,10,5,2};
    assert(md_start());
    for(int i=0;i<22;i++){test_clock+=150;assert(md_frame(1,1,bands,70,sceKernelGetSystemTimeWide(),5)==1);assert(!xv_failed());}
    assert(cave_scene&&cave_scene->next>0);assert(vertices>100);
    assert((uintptr_t)cave_scene%64==0);
    md_cave_control(1,128,128,0,0,0,sceKernelGetSystemTimeWide());
    assert(xbox_cave_phase()==CAVE_GAME_INTRO);
    md_cave_game_menu(0,1,1,0);assert(xbox_cave_phase()==CAVE_GAME_ALIVE);
    test_clock+=150;assert(md_frame(1,1,bands,70,sceKernelGetSystemTimeWide(),5)==1);
    md_stop();assert(!test_allocations);
    assert(md_start());MdFileError error;
    assert(md_load_transition(argv[1],0,&error)==MD_FILE_OK);
    short pcm[2304];for(int i=0;i<2304;i++)pcm[i]=(i%128)*200-12800;
    for(int i=0;i<4;i++){test_clock+=150;visualization_pcm_publish(pcm,1152);assert(md_frame(1,1,bands,70,sceKernelGetSystemTimeWide(),3)==1);assert(!xv_failed());}
    assert(md_load_transition(argv[2],1000,&error)==MD_FILE_OK);
    assert(md_live);
    for(int i=0;i<8;i++){test_clock+=150;visualization_pcm_publish(pcm,1152);assert(md_frame(1,0,bands,70,sceKernelGetSystemTimeWide(),3)==1);assert(!xv_failed());}
    md_stop();md_free_preset(&md_custom_preset);assert(!test_allocations);
    puts("Xbox shared effects: Monkey flight, presets, live transition and teardown passed");
}
