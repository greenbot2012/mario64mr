// Stand-ins for the handful of libultra / N64 OS calls Goddard makes. On the console these drive DMA,
// the TLB, the VI and the controller; here everything is already in host memory and input comes from the API.
#include <string.h>
#include <stdint.h>
#include <ultra64.h>

uintptr_t osVirtualToPhysical(void *addr) { return (uintptr_t) addr; }

static u64 sTicks;
OSTime osGetTime(void) { return sTicks += 46875; } // 1 ms of the 46.875 MHz counter per call; only feeds debug timers

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count) { (void) mq; (void) msg; (void) count; }
s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag) { (void) mq; (void) msg; (void) flag; return 0; }
void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg) { (void) e; (void) mq; (void) msg; }
s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 dir, uintptr_t devAddr, void *vAddr, size_t nbytes, OSMesgQueue *mq) {
    (void) mb; (void) pri; (void) dir; (void) mq;
    memcpy(vAddr, (const void *) devAddr, nbytes); // "ROM" addresses are host pointers here
    return 0;
}
void osViSetSpecialFeatures(u32 func) { (void) func; }
void osViSwapBuffer(void *frameBufPtr) { (void) frameBufPtr; }
s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *status) { (void) mq; *bitpattern = 1; (void) status; return 0; }
s32 osContStartReadData(OSMesgQueue *mq) { (void) mq; return 0; }
void osUnmapTLBAll(void) {}
void osMapTLB(s32 index, OSPageMask pm, void *vaddr, u32 evenpaddr, u32 oddpaddr, s32 asid) {
    (void) index; (void) pm; (void) vaddr; (void) evenpaddr; (void) oddpaddr; (void) asid;
}

// The dynlists are linked in rather than loaded from a ROM segment (NO_SEGMENTED_MEMORY path)
u8 _gd_dynlistsSegmentRomStart[1];
u8 _gd_dynlistsSegmentRomEnd[1];
