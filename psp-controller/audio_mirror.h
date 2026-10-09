/* SPDX-License-Identifier: GPL-2.0-or-later
 * Normal mixer and SRC PCM mirror, verified 6.61 layout only. Sony remains audio master.
 * Atomic JAL patches; original delay slots and DDR flush are preserved.
 * No file/USB/allocator/semaphore calls from the high-priority mixer hook.
 */
#include "audio_mirror_signature.h"
#include "audio_mirror_src.h"
#include "../streammaster/spdif_protocol.h"
#include "audio_mirror_flow.h"
_Static_assert(sizeof(SmAudioWrite)+MIRROR_PACKET_MAX*4<=SM_PAYLOAD_SIZE,"PCM packet fits existing RPC buffer");
#define MIRROR_LOG_COUNT 8U
typedef struct { char text[128]; int rc; unsigned sec,usec; } MirrorLog;
typedef struct {
    volatile unsigned rd,wr,rate,enabled,fault,users,blocks,dropped;
    volatile unsigned log_rd,log_wr,log_lost;
    unsigned trimmed,gap_us,rpc_us;
    unsigned diag_calls,diag_busy,diag_missing,diag_wall,diag_submit,diag_reply,diag_wake;
    volatile unsigned capture_calls,capture_last,capture_gap,capture_late,capture_repeat,capture_half;
    volatile unsigned src_rd,src_wr,src_rate;
    unsigned src_phase;
    unsigned *src_words;
    MirrorLog log[MIRROR_LOG_COUNT];
    unsigned words[MIRROR_RING];
    SmFrame rpc;
} AudioMirror;
static int audio_mirror_enabled;
static int audio_mirror_vsh;
static int audio_mirror_pinned;
static volatile int audio_mirror_allowed,audio_mirror_running;
static SceUID audio_mirror_thread=-1,audio_mirror_memory=-1;
static AudioMirror *audio_mirror;
static volatile unsigned char *audio_mirror_state;
static volatile uint32_t *audio_mirror_call;
static uint32_t audio_mirror_saved,audio_mirror_jump;
static int (*audio_mirror_flush_original)(int);
static SceUID audio_mirror_src_memory=-1;
static volatile uint32_t *audio_mirror_src_call;
static uint32_t audio_mirror_src_saved,audio_mirror_src_jump;
static int (*audio_mirror_src_original)(int,void *);

/* The verified internal call is made with interrupts disabled. Mirror only
 * buffers accepted by Sony; never retain a game's buffer, wait, or allocate.
 * Output2 enters this same SRC routine. Sony owns pacing and the original DAC. */
static int audio_mirror_src_output(int volume,void *buffer) {
    AudioMirror *m=audio_mirror;m->users++;
    int rc=audio_mirror_src_original(volume,buffer);
    if(rc>0 && buffer && m->enabled && m->src_words && audio_mirror_allowed && !app_owner) {
        unsigned rate=*(volatile unsigned short *)(audio_mirror_state+1368);
        unsigned count=(unsigned)rc,wr=m->src_wr;
        if(rate!=m->src_rate || audio_mirror_state[1374]!=4 || count>4111)m->fault=3;
        else if(count>MIRROR_SRC_RING-(wr-m->src_rd))m->dropped+=count;
        else {
            const unsigned char *pcm=buffer;
            for(unsigned i=0;i<count;i++) {
                unsigned word;memcpy(&word,pcm+4*i,4);
                m->src_words[(wr+i)&(MIRROR_SRC_RING-1)]=(volume>>5)==1024?word:mirror_src_sample(word,(unsigned)volume);
            }
            __asm__ volatile("sync" ::: "memory");m->src_wr=wr+count;m->blocks++;
        }
    }
    m->users--;return rc;
}
static void audio_mirror_src_fill(AudioMirror *m) {
    unsigned rd=m->src_rd,wr=m->wr,phase=m->src_phase;
    unsigned end=m->src_wr,space=MIRROR_RING-(wr-m->rd);
    if(!m->src_words)return;
    while(space && end-rd>(m->src_rate==m->rate?0U:1U)) {
        unsigned word=m->src_words[rd&(MIRROR_SRC_RING-1)];
        if(m->src_rate==m->rate)rd++;
        else {
            word=mirror_src_lerp(word,m->src_words[(rd+1)&(MIRROR_SRC_RING-1)],phase,m->rate);
            phase+=m->src_rate;rd+=phase/m->rate;phase%=m->rate;
        }
        m->words[wr++&(MIRROR_RING-1)]=word;space--;
    }
    __asm__ volatile("sync" ::: "memory");m->src_rd=rd;m->src_phase=phase;m->wr=wr;
}

/* Never write the Memory Stick from the time-sensitive PCM worker. The
 * existing service thread drains at most one bounded diagnostic per tick.
 * A full diagnostic queue drops diagnostics, not audio. */
static void audio_mirror_log(const char *text,int rc) {
    AudioMirror *m=audio_mirror;
    if(!controller_report || !m)return;
    MirrorLog item;memset(&item,0,sizeof(item));
    snprintf(item.text,sizeof(item.text),"%s",text);item.rc=rc;
    unsigned long long now=sceKernelGetSystemTimeWide();
    item.sec=now/1000000;item.usec=now%1000000;
    int intr=sceKernelCpuSuspendIntr();
    if(m->log_wr-m->log_rd<MIRROR_LOG_COUNT) {
        m->log[m->log_wr&(MIRROR_LOG_COUNT-1)]=item;m->log_wr++;
    } else m->log_lost++;
    sceKernelCpuResumeIntr(intr);
}
static void audio_mirror_log_drain(void) {
    AudioMirror *m=audio_mirror;MirrorLog item;int have=0;
    if(!m)return;
    int intr=sceKernelCpuSuspendIntr();
    if(m->log_wr!=m->log_rd){item=m->log[m->log_rd&(MIRROR_LOG_COUNT-1)];m->log_rd++;have=1;}
    sceKernelCpuResumeIntr(intr);
    if(have){char line[160];snprintf(line,sizeof(line),"at %u.%06u %s",item.sec,item.usec,item.text);controller_log(line,item.rc);}
}

#include "pops_audio.h"
static int audio_mirror_flush(int mask) {
    AudioMirror *m=audio_mirror;
    m->users++;
    if(m->enabled && !m->src_rate && !*(volatile unsigned short *)(audio_mirror_state+1368) && audio_mirror_allowed && !app_owner) {
        unsigned rate=*(volatile unsigned short *)(audio_mirror_state+1360);
        volatile uint32_t *d=(volatile uint32_t *)(audio_mirror_state+1024);
        /* Before the original flush, the new block terminates the DMA chain;
         * the other half points to it. Do not infer the half from DMA progress. */
        unsigned physical=(uintptr_t)audio_mirror_state&0x1fffffffU;
        int half=mirror_dma_half(d[6],d[14],physical);
        if(half<0){m->fault=1;goto done;}
        if(rate!=m->rate){m->fault=2;goto done;}
        /* VSH-only counters: no PCM dump, allocation, file or USB operation
         * from the mixer. Repeat halves/gaps are evidence, not errors: the
         * driver may legitimately stop and restart between menu sounds. */
        if(audio_mirror_vsh && controller_report) {
            unsigned stamp=sceKernelGetSystemTimeLow();
            if(m->capture_calls) {
                unsigned gap=stamp-m->capture_last;
                if(gap>m->capture_gap)m->capture_gap=gap;
                if(gap>5000)m->capture_late++;
                if(m->capture_half==(unsigned)half)m->capture_repeat++;
            }
            m->capture_last=stamp;m->capture_half=half;m->capture_calls++;
        }
        unsigned wr=m->wr;
        if(wr-m->rd>MIRROR_RING-64){m->dropped+=64;goto done;}
        volatile unsigned *pcm=(volatile unsigned *)(audio_mirror_state+half*256);
        for(unsigned i=0;i<64;i++)m->words[(wr+i)&(MIRROR_RING-1)]=pcm[i];
        __asm__ volatile("sync" ::: "memory");
        m->wr=wr+64;m->blocks++;
    }
done:
    /* Original call remains part of every mixer iteration, even if our USB
     * transport is unavailable. Do not change its return value or ordering. */
    int rc=audio_mirror_flush_original(mask);
    m->users--;
    return rc;
}
static int audio_mirror_install(void) {
    if(sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_POPS)return pops_audio_install();
    SceModule *mod=sceKernelFindModuleByName("sceAudio_Driver");
    if(sceKernelDevkitVersion()!=0x06060110 || !mod || mod->nsegment!=2 || mod->data_size!=64 || mod->bss_size!=1400 ||
       !audio_probe_text_valid(mod->text_addr,mod->text_size,mod->segmentaddr,mod->segmentsize,mod->nsegment) ||
       mod->segmentsize[1]<1464 || mod->segmentaddr[1]<0x88000000U || mod->segmentaddr[1]>0x8bfff000U ||
       !mirror_signature((const uint32_t *)mod->text_addr,mod->text_size,mod->text_addr,mod->segmentaddr[1]+64))return -1;
    unsigned *code=(unsigned *)mod->text_addr;
    unsigned target=mirror_jtarget(code[0x2f10/4],mod->text_addr+0x2f10);
    /* The imported flush stub must still resolve to resident kernel code. */
    if(target<0x88000000U || target>=0x8c000000U)return -2;
    audio_mirror_state=(void *)((mod->segmentaddr[1]+64)|0x20000000U);
    audio_mirror_flush_original=(void *)(mod->text_addr+0x2f10);
    audio_mirror_call=(void *)(mod->text_addr+0x46c);
    audio_mirror_saved=*audio_mirror_call;
    audio_mirror_jump=0x0c000000U|(((uintptr_t)audio_mirror_flush>>2)&0x03ffffffU);
    if(((uintptr_t)audio_mirror_flush&0xf0000000U)!=(mod->text_addr&0xf0000000U))return -3;
    if(mirror_src_signature((const uint32_t *)code,mod->text_size,mod->text_addr,mod->segmentaddr[1]+64) &&
       ((uintptr_t)audio_mirror_src_output&0xf0000000U)==(mod->text_addr&0xf0000000U)) {
        audio_mirror_src_original=(void *)(mod->text_addr+0x225c);
        audio_mirror_src_call=(void *)(mod->text_addr+0x2170);
        audio_mirror_src_saved=*audio_mirror_src_call;
        audio_mirror_src_jump=0x0c000000U|(((uintptr_t)audio_mirror_src_output>>2)&0x03ffffffU);
    }
    int intr=sceKernelCpuSuspendIntr();
    *audio_mirror_call=audio_mirror_jump;
    if(audio_mirror_src_call)*audio_mirror_src_call=audio_mirror_src_jump;
    sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
    sceKernelCpuResumeIntr(intr);
    audio_mirror_log("PCM mirror verified mixer JAL installed",mod->text_addr+0x46c);
    audio_mirror_log("PCM mirror SRC/Output2 verified JAL",audio_mirror_src_call?(int)audio_mirror_src_call:-1);
    return 0;
}
static void audio_mirror_uninstall(void) {
    if(pops_audio_mode){pops_audio_uninstall();return;}
    if(!audio_mirror_call)return;
    audio_mirror->enabled=0;
    int intr=sceKernelCpuSuspendIntr();
    if(audio_mirror_src_call) {
        if(*audio_mirror_src_call==audio_mirror_src_jump)*audio_mirror_src_call=audio_mirror_src_saved;
        else if(*audio_mirror_src_call!=audio_mirror_src_saved)audio_mirror_pinned=1;
        sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
    }
    if(*audio_mirror_call==audio_mirror_jump) {
        *audio_mirror_call=audio_mirror_saved;
        sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
    } else if(*audio_mirror_call!=audio_mirror_saved) {
        /* Another hook may chain into ours: never free its code/context. */
        audio_mirror_pinned=1;
    }
    sceKernelCpuResumeIntr(intr);
    while(audio_mirror->users)sceKernelDelayThread(1000);
    if(audio_mirror_pinned)audio_mirror_log("PCM mirror foreign hook: unload refused",-1);
    audio_mirror_call=NULL;
    audio_mirror_src_call=NULL;
}
static int audio_mirror_rpc(unsigned op,const void *data,unsigned length,SmAudioStatus *status,int closing) {
    if(length>SM_PAYLOAD_SIZE)return SM_INVALID;
    if(sceKernelPollSema(lock_id,1)<0){if(controller_report)audio_mirror->diag_busy++;return SM_BUSY;}
    int rc=SM_OFFLINE;
    if(started && attached && (closing || (audio_mirror_allowed && !app_owner))) {
        static unsigned sequence;
        SmFrame *f=&audio_mirror->rpc;
        /* PCM payload can already be in place; preserve it in that case. */
        if(data!=f->payload && length)memcpy(f->payload,data,length);
        memset(f,0,32);f->op=op;f->sequence=++sequence;f->length=length;sm_seal(f);
        unsigned long long begin=sceKernelGetSystemTimeWide();
        pcm_diag_done=0;pcm_diag_active=controller_report;
        rc=exchange_begin(f,1);
        unsigned submitted=controller_report?sceKernelGetSystemTimeLow():0;
        if(rc>=0)rc=exchange_finish(f);
        unsigned end=sceKernelGetSystemTimeLow();
        pcm_diag_active=0;
        unsigned elapsed=end-(unsigned)begin;
        if(controller_report) {
            AudioMirror *m=audio_mirror;
            m->diag_calls++;m->diag_wall+=elapsed;
            unsigned submit=submitted-(unsigned)begin;
            if(submit>m->diag_submit)m->diag_submit=submit;
            if(rc>=0 && pcm_diag_done==3) {
                unsigned reply,wake;
                if(mirror_rpc_timing((unsigned)begin,pcm_diag_send,pcm_diag_recv,end,&reply,&wake)) {
                    if(reply>m->diag_reply)m->diag_reply=reply;
                    if(wake>m->diag_wake)m->diag_wake=wake;
                } else m->diag_missing++;
            } else m->diag_missing++;
        }
        if(elapsed>audio_mirror->rpc_us)audio_mirror->rpc_us=elapsed;
        if(rc>=0) {
            rc=f->result;
            if(rc==SM_OK && status) {
                if(f->length!=sizeof(*status))rc=SM_IO;else memcpy(status,f->payload,sizeof(*status));
            }
        }
    }
    sceKernelSignalSema(lock_id,1);return rc;
}
static int audio_mirror_close(SmAudioStatus *status) {
    audio_mirror->enabled=0;
    if(status->session && started && attached && !poisoned) {
        uint32_t session=status->session;
        int rc=audio_mirror_rpc(SM_AUDIO_CLOSE,&session,sizeof(session),NULL,1);
        /* A busy bus is retried by the worker; never close an app's new session. */
        if(rc==SM_BUSY && !app_owner)return 0;
    }
    memset(status,0,sizeof(*status));
    audio_mirror->rd=audio_mirror->wr;
    return 1;
}
static int audio_mirror_worker(SceSize size,void *args) {
    (void)size;(void)args;
    SmAudioStatus status={0};unsigned sequence=0,played=0,sent=0;
    unsigned long long retry=0,next_report=0,last_progress=0,next_status=0,last_loop=0;
    int paused=1,failed=0,last_gate=-1;
    unsigned long long diag_clock=0,diag_time=0;
    unsigned diag_preempt=0;
    int rc=audio_mirror_install();
    if(rc<0){audio_mirror_log("PCM mirror unsupported layout: no patch",rc);return 0;}
    while(audio_mirror_running) {
        AudioMirror *m=audio_mirror;
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(last_loop && now-last_loop>m->gap_us)m->gap_us=(unsigned)(now-last_loop);
        last_loop=now;
        if(pops_audio_mode)pops_audio_collect(m);
        unsigned rate=pops_audio_mode?44100:*(volatile unsigned short *)(audio_mirror_state+1360);
        unsigned src=pops_audio_mode?0:*(volatile unsigned short *)(audio_mirror_state+1368);
        if(src)rate=mirror_src_rate(src);
        if(status.session && (m->src_rate!=src || m->rate!=rate)) {
            if(!audio_mirror_close(&status)){sceKernelDelayThread(2000);continue;}
        }
        int gate=(!audio_mirror_allowed || app_owner)?1:(!started || !attached)?2:poisoned?3:(src && !audio_mirror_src_call)?4:(!rate || (!src && rate!=44100 && rate!=48000))?5:(pops_audio_mode && !pops_audio_published)?6:0;
        int usable=gate==0;
        if(gate!=last_gate){audio_mirror_log("PCM mirror gate (0 ready/1 context/2 USB/3 transport/4 SRC/5 rate/6 ME bootstrap)",gate);last_gate=gate;}
        if(!usable || failed || m->fault) {
            if(m->fault){audio_mirror_log("PCM mirror capture fault (1 half/2 rate)",m->fault);m->fault=0;failed=1;}
            int closed=audio_mirror_close(&status);
            if(failed && closed){retry=now+2000000;failed=0;}
            sceKernelDelayThread(10000);continue;
        }
        if(now<retry){sceKernelDelayThread(10000);continue;}
        if(!status.session) {
            if(src && !m->src_words) {
                audio_mirror_src_memory=sceKernelAllocPartitionMemory(1,"Consolizer SRC",PSP_SMEM_High,MIRROR_SRC_RING*4,NULL);
                if(audio_mirror_src_memory<0){audio_mirror_log("PCM SRC buffer allocation failed",audio_mirror_src_memory);retry=now+2000000;continue;}
                m->src_words=sceKernelGetBlockHeadAddr(audio_mirror_src_memory);
                if(!m->src_words){sceKernelFreePartitionMemory(audio_mirror_src_memory);audio_mirror_src_memory=-1;audio_mirror_log("PCM SRC missing allocation address",-1);retry=now+2000000;continue;}
            } else if(!src && m->src_words) {
                m->src_words=NULL;sceKernelFreePartitionMemory(audio_mirror_src_memory);audio_mirror_src_memory=-1;
            }
            m->src_rate=src;m->src_rd=m->src_wr=0;m->src_phase=0;
            SmAudioOpen open={rate,0};
            rc=audio_mirror_rpc(SM_AUDIO_OPEN,&open,sizeof(open),&status,0);
            if(rc==SM_BUSY){sceKernelDelayThread(5000);continue;}
            if(rc<0){audio_mirror_log("PCM mirror OPEN failed",rc);retry=now+2000000;sceKernelDelayThread(10000);continue;}
            audio_mirror_log("PCM mirror opened rate",rate);
            m->rd=m->wr;m->rate=rate;m->fault=0;sequence=sent=played=0;paused=1;last_progress=now;
            __asm__ volatile("sync" ::: "memory");m->enabled=1;
        }
        if(src)audio_mirror_src_fill(m);
        unsigned available=m->wr-m->rd;
        unsigned trim=src?0:mirror_trim_frames(available);
        if(trim){m->rd+=trim;m->trimmed+=trim;available-=trim;}
        unsigned frames=mirror_packet_frames(available,status.accepted-status.completed,status.space);
        int fresh_burst=mirror_fresh_burst(status.accepted,status.completed,frames);
        if(frames) {
            SmAudioWrite write={status.session,sequence,(uint32_t)((uint64_t)sent*1000/rate),frames};
            unsigned rd=m->rd;
            memcpy(m->rpc.payload,&write,sizeof(write));
            for(unsigned i=0;i<frames;i++)memcpy(m->rpc.payload+sizeof(write)+4*i,&m->words[(rd+i)&(MIRROR_RING-1)],4);
            rc=audio_mirror_rpc(SM_AUDIO_WRITE,m->rpc.payload,sizeof(write)+frames*4,&status,0);
            if(rc==SM_OK){
                m->rd=rd+frames;sent+=frames;sequence++;
                /* Menu/game sounds can be seconds apart. The old progress
                 * stamp belongs to the previous sound, not this new burst. */
                if(fresh_burst)last_progress=now;
            }
        } else {
            if(now<next_status){sceKernelDelayThread(2000);continue;}
            rc=audio_mirror_rpc(SM_AUDIO_STATUS,&status.session,sizeof(status.session),&status,0);
        }
        if(rc==SM_BUSY){sceKernelDelayThread(2000);continue;}
        if(rc<0){audio_mirror_log("PCM mirror USB write/status failed",rc);failed=1;continue;}
        /* WRITE already returned the same status snapshot. Avoid an immediate
         * redundant STATUS round trip after draining the local capture ring. */
        next_status=sceKernelGetSystemTimeWide()+10000;
        if(paused && status.accepted>=2304) {
            SmAudioPause pause={status.session,0};
            rc=audio_mirror_rpc(SM_AUDIO_PAUSE,&pause,sizeof(pause),&status,0);
            if(rc==SM_OK){paused=0;last_progress=now;}
            else if(rc!=SM_BUSY){audio_mirror_log("PCM mirror unpause failed",rc);failed=1;}
        }
        if(status.completed!=played){played=status.completed;last_progress=now;}
        if(!paused && status.accepted>status.completed && now-last_progress>2000000) {
            audio_mirror_log("PCM mirror DMA stalled",status.completed);failed=1;
        }
        if(now>=next_report) {
            char line[160];snprintf(line,sizeof(line),"PCM mirror blocks=%u ring=%u ESP=%u sent=%u played=%u underruns=%u dropped=%u",
                m->blocks,m->wr-m->rd,(unsigned)(status.accepted-status.completed),sent,(unsigned)status.completed,(unsigned)status.underruns,m->dropped);
            audio_mirror_log(line,0);
            snprintf(line,sizeof(line),"PCM service gap_us=%u rpc_us=%u trimmed=%u log_lost=%u",m->gap_us,m->rpc_us,m->trimmed,m->log_lost);
            audio_mirror_log(line,0);m->gap_us=m->rpc_us=0;next_report=now+5000000;
            if(audio_mirror_vsh && controller_report) {
                unsigned calls,gap,late,repeat;
                int intr=sceKernelCpuSuspendIntr();
                calls=m->capture_calls;gap=m->capture_gap;late=m->capture_late;repeat=m->capture_repeat;
                sceKernelCpuResumeIntr(intr);
                snprintf(line,sizeof(line),"VSH capture calls=%u gap_max_us=%u gaps_gt5ms=%u same_half=%u rate=%u",
                    calls,gap,late,repeat,rate);
                audio_mirror_log(line,0);
            }
            if(controller_report) {
                snprintf(line,sizeof(line),"PCM RPC n=%u busy=%u missing=%u wall_us=%u submit_max=%u reply_max=%u wake_max=%u",
                    m->diag_calls,m->diag_busy,m->diag_missing,m->diag_wall,m->diag_submit,m->diag_reply,m->diag_wake);
                audio_mirror_log(line,0);
                m->diag_calls=m->diag_busy=m->diag_missing=m->diag_wall=0;
                m->diag_submit=m->diag_reply=m->diag_wake=0;
                SceKernelThreadInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);
                int irc=sceKernelReferThreadStatus(0,&info);
                if(irc>=0) {
                    unsigned long long clocks=((unsigned long long)info.runClocks.hi<<32)|info.runClocks.low;
                    unsigned long long stamp=sceKernelGetSystemTimeWide();
                    if(diag_time) {
                        snprintf(line,sizeof(line),"PCM thread wall_us=%u run_ticks=%u preempt=%u priority=%d stack_free=%d",
                            (unsigned)(stamp-diag_time),(unsigned)(clocks-diag_clock),
                            info.threadPreemptCount-diag_preempt,info.currentPriority,sceKernelCheckThreadStack());
                        audio_mirror_log(line,0);
                    }
                    diag_clock=clocks;diag_time=stamp;diag_preempt=info.threadPreemptCount;
                } else audio_mirror_log("PCM thread timing unavailable",irc);
            }
            if(pops_audio_mode && pops_audio_shared)audio_mirror_log("POPS ME callback samples",pops_audio_shared->calls);
        }
        sceKernelDelayThread(2000);
    }
    audio_mirror->enabled=0;
    /* Stop hooks before freeing memory. On a busy app handover, leave the
     * old session alone: the app OPEN replaces it and stale IDs cannot close it. */
    audio_mirror_close(&status);
    audio_mirror_uninstall();
    if(audio_mirror_src_memory>=0 && !audio_mirror_pinned){sceKernelFreePartitionMemory(audio_mirror_src_memory);audio_mirror_src_memory=-1;audio_mirror->src_words=NULL;}
    audio_mirror_log("PCM mirror stopped",0);return 0;
}
static void audio_mirror_start(void) {
    /* Context/title policy is already checked by controller_worker. VSH and
     * POPS may use the same verified mixer; do not exclude them by context.
     * Actual driver signature and SRC-path checks remain authoritative. */
    if(!audio_mirror_enabled || sceKernelFindModuleByName("PSPStreamer"))return;
    audio_mirror_memory=sceKernelAllocPartitionMemory(1,"Consolizer PCM",PSP_SMEM_High,sizeof(AudioMirror),NULL);
    if(audio_mirror_memory<0){controller_log("PCM mirror allocation failed",audio_mirror_memory);return;}
    audio_mirror=sceKernelGetBlockHeadAddr(audio_mirror_memory);
    if(!audio_mirror){sceKernelFreePartitionMemory(audio_mirror_memory);audio_mirror_memory=-1;controller_log("PCM mirror missing allocation address",-1);return;}
    memset(audio_mirror,0,sizeof(*audio_mirror));
    audio_mirror_vsh=sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_VSH;
    audio_mirror_running=1;
    /* Captured audio has a bounded deadline: lobby/loading threads can starve
     * priority 0x28 even after both USB callbacks completed. Prioritize only
     * our transport worker (smaller value = higher PSP priority). Keep the
     * blocking USB waits, bounded packets and 2 ms yield; do not alter Sony's
     * mixer, game threads or USB callbacks, or allocate a larger audio ring. */
    audio_mirror_thread=sceKernelCreateThread("Consolizer PCM",audio_mirror_worker,0x18,4096,0,NULL);
    int rc=audio_mirror_thread<0?audio_mirror_thread:sceKernelStartThread(audio_mirror_thread,0,NULL);
    if(rc<0) {
        controller_log("PCM mirror worker failed",rc);audio_mirror_running=0;
        if(audio_mirror_thread>=0)sceKernelDeleteThread(audio_mirror_thread);
        audio_mirror_thread=-1;sceKernelFreePartitionMemory(audio_mirror_memory);audio_mirror_memory=-1;audio_mirror=NULL;
    }
}
static void audio_mirror_stop(void) {
    audio_mirror_allowed=0;audio_mirror_running=0;
    if(audio_mirror_thread>=0) {
        /* No forced termination: a hook or USB request must not outlive us. */
        sceKernelWaitThreadEnd(audio_mirror_thread,NULL);sceKernelDeleteThread(audio_mirror_thread);audio_mirror_thread=-1;
    }
    if(audio_mirror)while(audio_mirror->log_rd!=audio_mirror->log_wr)audio_mirror_log_drain();
    if(audio_mirror_memory>=0 && !audio_mirror_pinned){sceKernelFreePartitionMemory(audio_mirror_memory);audio_mirror_memory=-1;audio_mirror=NULL;}
}
