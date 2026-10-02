/* SPDX-License-Identifier: GPL-2.0-or-later
 * Opt-in POPS serial interception, not a system input hook. The user-mode
 * payload contains no references to this kernel module, even during unload.
 */
#include "pops_serial.h"
#include "pops_signature.h"
#include "bridge/pops_payload.inc"
static int pops_rumble_enabled;
static PopsSerial *pops_rumble_context;
static volatile uint32_t *pops_rumble_call;
static uint32_t pops_rumble_jump;
static SceUID pops_rumble_module=-1;
static unsigned long long pops_rumble_next;
static int pops_rumble_attempted;

static uint32_t pops_jump(uintptr_t address) { return 0x0c000000u | ((address >> 2) & 0x03ffffffu); }

/* Validate BOTH the ordinary handler and the serial dispatch site. Offsets
 * describe the inspected 6.60 03g layout, not a claimed universal firmware
 * address. 6.61 is accepted only if its relocated code matches these guards.
 * Other layouts remain completely unmodified. */
static int pops_rumble_layout(const SceModule *mod) {
    if (mod->text_addr < 0x08800000u || mod->text_addr > 0x09f00000u ||
        mod->text_size < 0xa508 || mod->text_size > 0x02000000u - (mod->text_addr - 0x08000000u) ||
        mod->gp_value != 0x10000u) return 0;
    const uint32_t *f = (const uint32_t *)(mod->text_addr + 0xa250);
    const uint32_t *c = (const uint32_t *)(mod->text_addr + 0x9e88);
    return pops_serial_signature(f,c,mod->text_addr);
}

static void pops_rumble_install(SceModule *mod) {
    pops_rumble_attempted = 1;
    controller_log("POPS rumble module text", mod->text_addr);
    controller_log("POPS rumble module GP", mod->gp_value);
    if (!pops_rumble_layout(mod)) {
        controller_log("POPS rumble: unsupported signature; unchanged", 0);
        return;
    }
    /* User memory only; no framebuffer-sized allocation in kernel RAM. */
    unsigned code_size = (sizeof(pops_user_code) + 63u) & ~63u;
    unsigned bytes = code_size + 64 + sizeof(PopsSerial);
    SceUID block = sceKernelAllocPartitionMemory(2, "Consolizer POPS serial", PSP_SMEM_High, bytes, NULL);
    if (block < 0) { controller_log("POPS rumble user allocation", block); return; }
    unsigned char *mem = sceKernelGetBlockHeadAddr(block);
    if (!mem || (uintptr_t)mem < 0x08800000u || (uintptr_t)mem + bytes > 0x0a000000u) {
        sceKernelFreePartitionMemory(block); controller_log("POPS rumble invalid user address", -1); return;
    }
    memcpy(mem, pops_user_code, sizeof(pops_user_code));
    uint32_t *stub = (uint32_t *)(mem + code_size);
    PopsSerial *ctx = (PopsSerial *)(mem + code_size + 64);
    memset(ctx, 0, sizeof(*ctx));
    uintptr_t original = mod->text_addr + 0xa250, context = (uintptr_t)ctx, payload = (uintptr_t)mem;
    ctx->original = (PopsOriginal)original;
    ctx->native = (volatile uint8_t *)0x13c00;
    ctx->enabled = 1;
    /* The serial dispatch also handles memory cards. Only the exact pad
     * target is replaced; jr v0 preserves every other target and the GP. */
    stub[0]=0x3c180000u | (original>>16);    /* lui t8, original_hi */
    stub[1]=0x37180000u | (original&65535); /* ori t8,t8, original_lo */
    stub[2]=0x10580003; stub[3]=0;         /* beq v0,t8,pad; nop */
    stub[4]=0x00400008; stub[5]=0;         /* jr v0; nop */
    stub[6]=0x3c070000u | (context>>16);   /* lui a3,context_hi */
    stub[7]=0x34e70000u | (context&65535);
    stub[8]=0x3c190000u | (payload>>16);
    stub[9]=0x37390000u | (payload&65535);
    stub[10]=0x03200008; stub[11]=0;       /* jr t9; nop */
    sceKernelDcacheWritebackInvalidateAll();
    sceKernelIcacheInvalidateAll();
    volatile uint32_t *call = (uint32_t *)(mod->text_addr + 0x9eac);
    uint32_t jump = pops_jump((uintptr_t)stub);
    int intr = sceKernelCpuSuspendIntr();
    if (*call != 0x0040f809) {
        sceKernelCpuResumeIntr(intr); sceKernelFreePartitionMemory(block);
        controller_log("POPS rumble dispatch changed; unchanged", -1); return;
    }
    /* One atomic instruction, original delay slot remains untouched. There
     * is no torn entry prologue and no trampoline back into kernel code. */
    *call = jump;
    sceKernelDcacheWritebackInvalidateAll();
    sceKernelIcacheInvalidateAll();
    pops_rumble_context=ctx; pops_rumble_call=call; pops_rumble_jump=jump;
    pops_rumble_module=mod->modid;
    sceKernelCpuResumeIntr(intr);
    controller_log("POPS DualShock capture installed; EP0 motor output", bytes);
}

static void pops_rumble_update(unsigned long long now, int allowed) {
    if (!pops_rumble_enabled || !allowed || sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_POPS ||
        now < pops_rumble_next) return;
    pops_rumble_next=now+2000000;
    if (!pops_rumble_attempted) {
        /* Loader already waits two seconds. Avoid probing the early boot
         * path; never register an unchainable global module-start hook. */
        if (now < 10000000) return;
        SceModule *mod=sceKernelFindModuleByName("pops");
        if (mod) pops_rumble_install(mod);
        return;
    }
    if (!pops_rumble_context) return;
    SceModule *current=sceKernelFindModuleByName("pops");
    if (!current || current->modid!=pops_rumble_module ||
        current->text_addr+0x9eac!=(uintptr_t)pops_rumble_call) {
        pops_rumble_context=NULL; pops_rumble_call=NULL;
        controller_log("POPS capture module gone; no stale access",0); return;
    }
    if (!controller_report) return;
    {
        SmPadMeta meta;unsigned replies,nonzero;
        int intr=sceKernelCpuSuspendIntr();meta=pad_meta;replies=rumble_replies;
        unsigned long long stamp=pad_meta_time;
        nonzero=rumble_nonzero_replies;
        sceKernelCpuResumeIntr(intr);
        char line[128];
        snprintf(line,sizeof(line),"Rumble transport backend=%u blocked=%u VID=%04X PID=%04X meta_fresh=%u replies",
            meta.reserved[0]&SM_RUMBLE_BACKEND,meta.reserved[1],meta.reserved[2]|(meta.reserved[3]<<8),
            meta.reserved[4]|(meta.reserved[5]<<8),(unsigned)(stamp && now>=stamp && now-stamp<15000000));
        controller_log(line,replies);
        snprintf(line,sizeof(line),"Rumble progress rx=%u rx_nz=%u tx_nz=%u ack_nz=%u PSP_nonzero",
            !!(meta.reserved[0]&SM_RUMBLE_RX_VALID),!!(meta.reserved[0]&SM_RUMBLE_RX_NONZERO),
            !!(meta.reserved[0]&SM_RUMBLE_TX_NONZERO),!!(meta.reserved[0]&SM_RUMBLE_ACK_NONZERO));
        controller_log(line,nonzero);
    }
    for (unsigned port=0;port<2;port++) {
        PopsPort p;
        int intr=sceKernelCpuSuspendIntr();
        memcpy(&p,&pops_rumble_context->port[port],sizeof(p));
        sceKernelCpuResumeIntr(intr);
        if (!p.commands && port) continue;
        char line[128];
        snprintf(line,sizeof(line),"POPS p%u cmds=%u poll=%u seen=%03X cfg=%u map=%02X/%02X motor=%u/%u peak=%u/%u",
            port,(unsigned)p.commands,(unsigned)p.polls,(unsigned)p.seen,p.config,p.map_enable,p.map_large,p.small,p.large,p.peak_small,p.peak_large);
        controller_log(line,p.changes);
    }
}
/* Latest-state mailbox only. Never submit USB or Bluetooth from the serial
 * interceptor. Stop if the game stops polling its pad, even with a nonzero
 * final motor byte. The existing worker calls this every input iteration. */
static void pops_rumble_publish(unsigned long long now,int active) {
    static uint32_t polls;
    static unsigned long long polled_at;
    unsigned small=0,large=0;
    if(active && pops_rumble_enabled && pops_rumble_context) {
        int intr=sceKernelCpuSuspendIntr();
        PopsPort p=pops_rumble_context->port[0];
        int enabled=pops_rumble_context->enabled;
        sceKernelCpuResumeIntr(intr);
        if(p.polls!=polls){polls=p.polls;polled_at=now;}
        if(enabled && !p.muted && sm_rumble_fresh(now,polled_at)){small=p.small;large=p.large;}
    } else polled_at=0;
    rumble_publish(small,large,now);
}
static void pops_rumble_stop(void) {
    rumble_publish(0,0,0);
    if (!pops_rumble_call) return;
    SceModule *current=sceKernelFindModuleByName("pops");
    int intr=sceKernelCpuSuspendIntr();
    if (current && current->modid==pops_rumble_module &&
        current->text_addr+0x9eac==(uintptr_t)pops_rumble_call && *pops_rumble_call == pops_rumble_jump) {
        *pops_rumble_call=0x0040f809;
        sceKernelDcacheWritebackInvalidateAll(); sceKernelIcacheInvalidateAll();
    }
    sceKernelCpuResumeIntr(intr);
    /* A suspended POPS thread may still be inside the payload. Retain this
     * single small user block until process exit; it has no kernel references.
     * Never free executable memory under such a thread. */
    pops_rumble_context=NULL; pops_rumble_call=NULL;
}
