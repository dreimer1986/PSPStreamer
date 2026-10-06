/* SPDX-License-Identifier: GPL-2.0-or-later
 * Opt-in digital audio worker. No PSP DAC/ME audio calls on this path.
 * Called after the common transport and timed-queue definitions in main.c.
 */
#include "../streammaster/spdif_protocol.h"
static void spdif_log_open(int result,unsigned length,unsigned rate,unsigned non_audio) {
    if(!debug_enabled)return;
    char line[256];
    snprintf(line,sizeof(line),"SPDIF open RPC rc=%d hex=%08X reply_bytes=%u rate=%u non_audio=%u\n",result,(unsigned)result,length,rate,non_audio);
    video_watch_write(line,0);
    SmAudioDiag d={0};unsigned bytes=0;
    int rc=stm_rpc(SM_AUDIO_DIAG,NULL,0,&d,sizeof(d),&bytes,&audio_running);
    if(rc==0 && bytes==sizeof(d)) {
        static const char *stages[]={"none","validate","ring","channel","mode/DMA","callback","silence","preload","enable","ready"};
        snprintf(line,sizeof(line),"SPDIF firmware stage=%u/%s error=%08X rate=%u non_audio=%u DMA_free=%u DMA_largest=%u PSRAM_free=%u preload_bytes=%u\n",
                 (unsigned)d.stage,d.stage<10?stages[d.stage]:"unknown",(unsigned)d.error,(unsigned)d.rate,
                 (unsigned)d.non_audio,(unsigned)d.dma_free,(unsigned)d.dma_largest,(unsigned)d.psram_free,(unsigned)d.loaded);
    } else snprintf(line,sizeof(line),"SPDIF firmware diagnostics unavailable rc=%d bytes=%u\n",rc,bytes);
    video_watch_write(line,0);
}
static int spdif_audio_thread(SceSize args,void *argp) {
    (void)args;(void)argp;
    const int music=!timed_active;
    TimedPacket packet={0};
    SmAudioStatus status={0};
    uint32_t sequence=0,rate=0,compressed=0,prefill=0;
    int reader=-1,queues=0,pause_state=1,rc=-1800,eof=0;
    unsigned long long progress=sceKernelGetSystemTimeWide();
    unsigned last_completed=0,length=0;
    unsigned long long open_busy_since=0;
    unsigned writes=0,polls=0,busy=0,min_frames=~0U,max_frames=0;
    if(offline_active || offline_music || !strncmp(audio_media_id,"radio.",6)) {
        rc=-1801;goto done; /* Never silently use another output. */
    }
    rc=stm_driver_start(0,&audio_running);
    if(rc<0)goto done;
    uint32_t caps=0;
    rc=stm_rpc(SM_CAPABILITIES,NULL,0,&caps,sizeof(caps),&length,&audio_running);
    if(rc<0 || length!=sizeof(caps) || !(caps&SM_CAP_SPDIF)){rc=-1802;goto done;}
    if(music) {
        timed_running=1;timed_eof=timed_error=timed_playing=0;
        timed_audio.free=timed_audio.ready=timed_video.free=timed_video.ready=-1;
        queues=1;
        if(timed_queue_init(&timed_audio)<0 || timed_queue_init(&timed_video)<0){rc=-1803;goto done;}
        snprintf(timed_request,sizeof(timed_request),
            "GET /api/transcode/%s?container=mp3&profile=%s&audio=0&audio_output=%s&start=%d HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s\r\n",
            audio_media_id,PSP_STREAMER_PROFILE,audio_output_keys[selected_audio_output],stream_start_seconds,server_host,server_auth_header);
        reader=sceKernelCreateThread("SPDIFReader",timed_reader,0x20,0x10000,0,NULL);
        if(reader<0){rc=reader;goto done;}
        rc=sceKernelStartThread(reader,0,NULL);
        if(rc<0){sceKernelDeleteThread(reader);reader=-1;goto done;}
    }
    audio_dac_samples=1; /* Progress counters below are 44.1k-equivalent samples. */
    audio_state=10;
    while(audio_running && (music || timed_running)) {
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(!packet.data && !eof) {
            timed_get(&timed_audio,&packet);
            if(!packet.data && timed_eof && timed_audio.read==timed_audio.write)eof=1;
        }
        if(timed_error){rc=timed_error;audio_transport_error=1;break;}
        if(packet.data) {
            uint32_t packet_rate,packet_compressed;
            if(packet.size<17 || memcmp(packet.data,"\xf0SMA1",5) || (packet.size-13)%4){rc=-1804;break;}
            memcpy(&packet_rate,packet.data+5,4);memcpy(&packet_compressed,packet.data+9,4);
            if(!status.session) {
                SmAudioOpen open={packet_rate,packet_compressed};
                rc=stm_rpc(SM_AUDIO_OPEN,&open,sizeof(open),&status,sizeof(status),&length,&audio_running);
                /* Contention is not an I2S initialization failure. Retry only
                 * BUSY, never an ambiguous timeout that may have opened DMA. */
                if(rc==SM_BUSY) {
                    if(!open_busy_since)open_busy_since=now;
                    if(now-open_busy_since<2000000ULL){sceKernelDelayThread(10000);continue;}
                }
                spdif_log_open(rc,length,packet_rate,packet_compressed);
                if(rc<0 || length!=sizeof(status)){memset(&status,0,sizeof(status));rc=-1805;break;}
                rate=packet_rate;compressed=packet_compressed;
                prefill=rate/5;
                /* Old optical firmware has a smaller ring: never gate video
                 * on an amount that cannot fit while output is paused. */
                if(prefill>status.space*3/4)prefill=status.space*3/4;
                progress=now;
                if(debug_enabled){char line[192];snprintf(line,sizeof(line),"SPDIF open rate=%u non_audio=%u session=%u output=%s\n",(unsigned)rate,(unsigned)compressed,(unsigned)status.session,optical_audio_label);video_watch_write(line,0);}
            } else if(rate!=packet_rate || compressed!=packet_compressed){rc=-1806;break;}
        }
        if(status.session) {
            int paused=!(audio_start && audio_queue_primed && (music || video_first_presented));
            if(paused!=pause_state) {
                SmAudioPause pause={status.session,paused};
                rc=stm_rpc(SM_AUDIO_PAUSE,&pause,sizeof(pause),&status,sizeof(status),&length,&audio_running);
                if(rc==SM_BUSY){busy++;sceKernelDelayThread(5000);continue;}
                if(rc<0 || length!=sizeof(status)){rc=-1807;break;}
                pause_state=paused;
            }
            if(packet.data && status.space>=(unsigned)(packet.size-13)/4) {
                unsigned char request[SM_PAYLOAD_SIZE];
                SmAudioWrite write={status.session,sequence,(uint32_t)packet.pts,(packet.size-13)/4};
                if(sizeof(write)+write.frames*4>sizeof(request)){rc=-1804;break;}
                memcpy(request,&write,sizeof(write));memcpy(request+sizeof(write),packet.data+13,write.frames*4);
                if(!compressed && playback_volume<30) {
                    short *pcm=(short *)(request+sizeof(write));
                    for(unsigned i=0;i<write.frames*2;i++)pcm[i]=(int)pcm[i]*playback_volume/30;
                }
                rc=stm_rpc(SM_AUDIO_WRITE,request,sizeof(write)+write.frames*4,&status,sizeof(status),&length,&audio_running);
                if(rc==SM_BUSY){busy++;sceKernelDelayThread(5000);continue;}
                if(rc<0 || length!=sizeof(status)){rc=-1808;break;}
                sequence++;
                writes++;if(write.frames<min_frames)min_frames=write.frames;
                if(write.frames>max_frames)max_frames=write.frames;
                if(!compressed)audio_measure_pcm((const short *)(request+sizeof(write)),write.frames);
                free(packet.data);memset(&packet,0,sizeof(packet));
            } else {
                uint32_t id=status.session;
                rc=stm_rpc(SM_AUDIO_STATUS,&id,sizeof(id),&status,sizeof(status),&length,&audio_running);
                polls++;
                if(rc==SM_BUSY){busy++;sceKernelDelayThread(5000);continue;}
                if(rc<0 || length!=sizeof(status)){rc=-1809;break;}
                sceKernelDelayThread(10000);
            }
            audio_blocks_published=(uint64_t)status.accepted*44100U/rate;
            audio_played_blocks=(uint64_t)status.completed*44100U/rate;
            if(status.playing){audio_current_timestamp_ms=status.position_ms;audio_clock_started=1;}
            if(status.accepted>=prefill || eof){audio_queue_primed=1;audio_state=15;}
            if(status.completed!=last_completed || paused){progress=now;last_completed=status.completed;}
            if(now-progress>15000000ULL){rc=-1810;break;}
            if(eof && status.completed==status.accepted){audio_clean_eof=1;rc=0;break;}
        } else if(eof){rc=-1811;break;}
    }
done:
    free(packet.data);
    if(debug_enabled){char line[160];snprintf(line,sizeof(line),"SPDIF transfers writes=%u polls=%u busy=%u frames_min=%u frames_max=%u\n",writes,polls,busy,writes?min_frames:0,max_frames);video_watch_write(line,0);}
    if(debug_enabled){char line[192];snprintf(line,sizeof(line),"SPDIF end rc=%d rate=%u accepted=%u completed=%u underrun_blocks=%u position_ms=%u\n",rc,(unsigned)rate,(unsigned)status.accepted,(unsigned)status.completed,(unsigned)status.underruns,(unsigned)status.position_ms);video_watch_write(line,0);}
    if(status.session){uint32_t id=status.session;stm_rpc(SM_AUDIO_CLOSE,&id,sizeof(id),NULL,0,NULL,NULL);}
    if(music) {
        timed_running=0;
        if(reader>=0){sceKernelWaitThreadEnd(reader,NULL);sceKernelDeleteThread(reader);}
        if(queues){timed_queue_destroy(&timed_audio);timed_queue_destroy(&timed_video);}
        audio_running=0;
    } else timed_audio_done=1;
    if(rc<0 && (music || timed_running))audio_state=rc;
    const short silence[2]={0,0};visualization_pcm_publish(silence,1);
    return 0;
}
