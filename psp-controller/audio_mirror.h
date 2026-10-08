/* SPDX-License-Identifier: GPL-2.0-or-later
 * Normal mixer PCM mirror, verified 6.61 layout only. Sony remains audio master.
 * Single atomic JAL patch; original delay slot and DDR flush are preserved.
 * No file/USB/allocator/semaphore calls from the high-priority mixer hook.
 */
#include "audio_mirror_signature.h"
#include "../streammaster/spdif_protocol.h"
#include "audio_mirror_flow.h"
#include "../streammaster/trace_protocol.h"
#define MIRROR_LOG_COUNT 8U
typedef struct { char text[128]; int rc; unsigned sec,usec; } MirrorLog;
typedef struct {
    volatile unsigned rd,wr,rate,enabled,fault,users,blocks,dropped;
    volatile unsigned log_rd,log_wr,log_lost;
    unsigned trimmed,gap_us,rpc_us;
    MirrorLog log[MIRROR_LOG_COUNT];
    unsigned words[MIRROR_RING];
    SmFrame rpc;
} AudioMirror;
static int audio_mirror_enabled;
static int audio_mirror_pinned;
static volatile int audio_mirror_allowed,audio_mirror_running;
static SceUID audio_mirror_thread=-1,audio_mirror_memory=-1;
static AudioMirror *audio_mirror;
static volatile unsigned char *audio_mirror_state;
static volatile uint32_t *audio_mirror_call;
static uint32_t audio_mirror_saved,audio_mirror_jump;
static int (*audio_mirror_flush_original)(int);

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

static int audio_mirror_flush(int mask) {
    AudioMirror *m=audio_mirror;
    m->users++;
    if(m->enabled && audio_mirror_allowed && !app_owner) {
        unsigned rate=*(volatile unsigned short *)(audio_mirror_state+1360);
        volatile uint32_t *d=(volatile uint32_t *)(audio_mirror_state+1024);
        /* Before the original flush, the new block terminates the DMA chain;
         * the other half points to it. Do not infer the half from DMA progress. */
        unsigned physical=(uintptr_t)audio_mirror_state&0x1fffffffU;
        int half=mirror_dma_half(d[6],d[14],physical);
        if(half<0){m->fault=1;goto done;}
        if(rate!=m->rate){m->fault=2;goto done;}
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
    int intr=sceKernelCpuSuspendIntr();
    *audio_mirror_call=audio_mirror_jump;
    sceKernelDcacheWritebackInvalidateAll();sceKernelIcacheInvalidateAll();
    sceKernelCpuResumeIntr(intr);
    audio_mirror_log("PCM mirror verified mixer JAL installed",mod->text_addr+0x46c);
    return 0;
}
static void audio_mirror_uninstall(void) {
    if(!audio_mirror_call)return;
    audio_mirror->enabled=0;
    int intr=sceKernelCpuSuspendIntr();
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
}
static int audio_mirror_rpc(unsigned op,const void *data,unsigned length,SmAudioStatus *status,int closing) {
    if(length>SM_PAYLOAD_SIZE)return SM_INVALID;
    if(sceKernelPollSema(lock_id,1)<0)return SM_BUSY;
    int rc=SM_OFFLINE;
    if(started && attached && (closing || (audio_mirror_allowed && !app_owner))) {
        static unsigned sequence;
        SmFrame *f=&audio_mirror->rpc;
        /* PCM payload can already be in place; preserve it in that case. */
        if(data!=f->payload && length)memcpy(f->payload,data,length);
        memset(f,0,32);f->op=op;f->sequence=++sequence;f->length=length;sm_seal(f);
        unsigned long long begin=sceKernelGetSystemTimeWide();
        rc=exchange_begin(f,1);
        if(rc>=0)rc=exchange_finish(f);
        unsigned elapsed=(unsigned)(sceKernelGetSystemTimeWide()-begin);
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
    int paused=1,failed=0,last_gate=-1,trace_supported=1,trace_started=0;
    int rc=audio_mirror_install();
    if(rc<0){audio_mirror_log("PCM mirror unsupported layout: no patch",rc);return 0;}
    while(audio_mirror_running) {
        AudioMirror *m=audio_mirror;
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(last_loop && now-last_loop>m->gap_us)m->gap_us=(unsigned)(now-last_loop);
        last_loop=now;
        unsigned rate=*(volatile unsigned short *)(audio_mirror_state+1360);
        unsigned src=*(volatile unsigned short *)(audio_mirror_state+1368);
        int gate=(!audio_mirror_allowed || app_owner)?1:(!started || !attached)?2:poisoned?3:src?4:(rate!=44100 && rate!=48000)?5:0;
        int usable=gate==0;
        if(gate!=last_gate){audio_mirror_log("PCM mirror gate (0 ready/1 context/2 USB/3 transport/4 SRC/5 rate)",gate);last_gate=gate;}
        if(!usable || failed || m->fault) {
            if(m->fault){audio_mirror_log("PCM mirror capture fault (1 half/2 rate)",m->fault);m->fault=0;failed=1;}
            int closed=audio_mirror_close(&status);
            if(failed && closed){retry=now+2000000;failed=0;}
            sceKernelDelayThread(10000);continue;
        }
        if(now<retry){sceKernelDelayThread(10000);continue;}
        if(!status.session) {
            if(controller_report && trace_supported && !trace_started) {
                uint32_t action=1;
                rc=audio_mirror_rpc(SM_DIAGNOSTICS,&action,sizeof(action),NULL,0);
                if(rc==SM_INVALID)trace_supported=0;
                if(rc==SM_OK)trace_started=1;
                audio_mirror_log("ESP diagnostics enable (HTTP port 8080)",rc);
            }
            SmAudioOpen open={rate,0};
            rc=audio_mirror_rpc(SM_AUDIO_OPEN,&open,sizeof(open),&status,0);
            if(rc==SM_BUSY){sceKernelDelayThread(5000);continue;}
            if(rc<0){audio_mirror_log("PCM mirror OPEN failed",rc);retry=now+2000000;sceKernelDelayThread(10000);continue;}
            audio_mirror_log("PCM mirror opened rate",rate);
            m->rd=m->wr;m->rate=rate;m->fault=0;sequence=sent=played=0;paused=1;last_progress=now;
            __asm__ volatile("sync" ::: "memory");m->enabled=1;
        }
        unsigned available=m->wr-m->rd;
        unsigned trim=mirror_trim_frames(available);
        if(trim){m->rd+=trim;m->trimmed+=trim;available-=trim;}
        if(available>=512 && status.accepted-status.completed<2048 && status.space>=512) {
            SmAudioWrite write={status.session,sequence,(uint32_t)((uint64_t)sent*1000/rate),512};
            unsigned rd=m->rd;
            memcpy(m->rpc.payload,&write,sizeof(write));
            for(unsigned i=0;i<512;i++)memcpy(m->rpc.payload+sizeof(write)+4*i,&m->words[(rd+i)&(MIRROR_RING-1)],4);
            rc=audio_mirror_rpc(SM_AUDIO_WRITE,m->rpc.payload,sizeof(write)+2048,&status,0);
            if(rc==SM_OK){m->rd=rd+512;sent+=512;sequence++;}
        } else {
            if(now<next_status){sceKernelDelayThread(2000);continue;}
            rc=audio_mirror_rpc(SM_AUDIO_STATUS,&status.session,sizeof(status.session),&status,0);
            next_status=now+10000;
        }
        if(rc==SM_BUSY){sceKernelDelayThread(2000);continue;}
        if(rc<0){audio_mirror_log("PCM mirror USB write/status failed",rc);failed=1;continue;}
        if(paused && status.accepted>=1536) {
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
            if(controller_report && trace_supported && trace_started) {
                uint32_t action=0;
                int trace_rc=audio_mirror_rpc(SM_DIAGNOSTICS,&action,sizeof(action),NULL,0);
                if(trace_rc<0 && trace_rc!=SM_BUSY)audio_mirror_log("ESP runtime fetch failed",trace_rc);
                if(trace_rc==SM_OK && m->rpc.length==sizeof(SmTrace)) {
                    SmTrace t;memcpy(&t,m->rpc.payload,sizeof(t));
                    if(t.version==1) {
                        snprintf(line,sizeof(line),"ESP audio flags=%u queued=%u played=%u under=%u dma=%u gap_us=%u",
                            (unsigned)t.audio_flags,(unsigned)t.audio_queued,(unsigned)t.audio_completed,(unsigned)t.audio_underruns,(unsigned)t.dma_callbacks,(unsigned)t.max_dma_gap_us);
                        audio_mirror_log(line,0);
                        snprintf(line,sizeof(line),"ESP USB errors=%u cmd_us=%u free=%u largest=%u reset=%u BT=%u HTTP=%x",
                            (unsigned)t.usb_events,(unsigned)t.max_command_us,(unsigned)t.internal_free,(unsigned)t.internal_largest,(unsigned)t.reset_reason,(unsigned)t.bt_state,(unsigned)t.http_error);
                        audio_mirror_log(line,0);
                        if(t.event_count) {
                            SmTraceEvent e=t.events[(t.event_count-1)%SM_TRACE_EVENTS];
                            snprintf(line,sizeof(line),"ESP last event ms=%u kind=%u a=%u b=%u",(unsigned)e.ms,(unsigned)e.kind,(unsigned)e.a,(unsigned)e.b);
                            audio_mirror_log(line,0);
                        }
                    }
                }
                snprintf(line,sizeof(line),"PSP PCM stack_free=%d kernel_free=%u largest=%u",
                    sceKernelGetThreadStackFreeSize(sceKernelGetThreadId()),sceKernelPartitionTotalFreeMemSize(1),sceKernelPartitionMaxFreeMemSize(1));
                audio_mirror_log(line,0);
            }
        }
        sceKernelDelayThread(2000);
    }
    audio_mirror->enabled=0;
    /* Stop hooks before freeing memory. On a busy app handover, leave the
     * old session alone: the app OPEN replaces it and stale IDs cannot close it. */
    audio_mirror_close(&status);
    audio_mirror_uninstall();
    audio_mirror_log("PCM mirror stopped",0);return 0;
}
static void audio_mirror_start(void) {
    if(!audio_mirror_enabled || sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME || sceKernelFindModuleByName("PSPStreamer"))return;
    audio_mirror_memory=sceKernelAllocPartitionMemory(1,"Consolizer PCM",PSP_SMEM_High,sizeof(AudioMirror),NULL);
    if(audio_mirror_memory<0){controller_log("PCM mirror allocation failed",audio_mirror_memory);return;}
    audio_mirror=sceKernelGetBlockHeadAddr(audio_mirror_memory);
    if(!audio_mirror){sceKernelFreePartitionMemory(audio_mirror_memory);audio_mirror_memory=-1;controller_log("PCM mirror missing allocation address",-1);return;}
    memset(audio_mirror,0,sizeof(*audio_mirror));
    audio_mirror_running=1;
    audio_mirror_thread=sceKernelCreateThread("Consolizer PCM",audio_mirror_worker,0x28,4096,0,NULL);
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
