// Lightweight statistical heap profiler via LD_PRELOAD.
// Attributes LIVE heap bytes to allocation call-stacks by sampling ~1/N allocations
// (weighted by liveness, not size), so both "one huge vector" and "millions of small
// nodes" surface. No valgrind shadow-memory overhead -> works on multi-GB peaks.
//
// Build:  gcc -O2 -fPIC -shared -o scripts/memprof.so scripts/memprof.c -ldl
// Use:    MEMPROF_EVERY=64 LD_PRELOAD=$PWD/scripts/memprof.so <cmd>   (dumps to stderr on exit)
//         send SIGUSR1 to dump mid-run without exiting.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <malloc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void* (*real_malloc)(size_t)         = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;
static void  (*real_free)(void*)            = NULL;

static __thread int in_hook = 0;     // reentrancy guard (backtrace() itself mallocs)
static uint64_t g_live_bytes = 0, g_peak_bytes = 0, g_sample_every = 64, g_counter = 0;

#define NSITES 4096
#define STKDEPTH 12
static struct Site { void* stk[STKDEPTH]; int n; uint64_t live_bytes; uint64_t samples; } g_sites[NSITES];
static int g_nsites = 0;

// Sampled allocations remember (ptr -> site,weight) so free() can decrement. Tiny open-addressed table.
#define NTRACK (1 << 20)
static struct Trk { void* p; int site; uint64_t w; } g_trk[NTRACK];
static uint64_t hshp(void* p){ uint64_t x=(uint64_t)p; x^=x>>33; x*=0xff51afd7ed558ccdULL; x^=x>>33; return x; }

static int find_site(void** stk, int n){
    for (int i=0;i<g_nsites;i++){ if (g_sites[i].n==n && memcmp(g_sites[i].stk,stk,n*sizeof(void*))==0) return i; }
    if (g_nsites>=NSITES) return -1;
    int i=g_nsites++; memcpy(g_sites[i].stk,stk,n*sizeof(void*)); g_sites[i].n=n; return i;
}
static void track_put(void* p, int site, uint64_t w){
    uint64_t h=hshp(p)&(NTRACK-1);
    for (int i=0;i<64;i++){ uint64_t j=(h+i)&(NTRACK-1); if(!g_trk[j].p){ g_trk[j].p=p; g_trk[j].site=site; g_trk[j].w=w; return; } }
}
static int track_take(void* p, uint64_t* w){
    uint64_t h=hshp(p)&(NTRACK-1);
    for (int i=0;i<64;i++){ uint64_t j=(h+i)&(NTRACK-1); if(g_trk[j].p==p){ int s=g_trk[j].site; *w=g_trk[j].w; g_trk[j].p=0; return s; } if(!g_trk[j].p) return -1; }
    return -1;
}

static void on_alloc(void* p, size_t n){
    if (!p || in_hook) return;
    size_t usable = malloc_usable_size(p);
    __sync_add_and_fetch(&g_live_bytes, usable);
    if (g_live_bytes > g_peak_bytes) g_peak_bytes = g_live_bytes;
    if ((++g_counter % g_sample_every) != 0) return;     // sample 1/every allocations
    in_hook = 1;
    void* stk[STKDEPTH+2]; int n2 = backtrace(stk, STKDEPTH+2);
    int off = n2>2?2:0;                                  // drop our hook frames
    int site = find_site(stk+off, n2-off>STKDEPTH?STKDEPTH:n2-off);
    if (site>=0){ uint64_t w = usable*g_sample_every; g_sites[site].live_bytes+=w; g_sites[site].samples++; track_put(p, site, w); }
    in_hook = 0;
}
static void on_free(void* p){
    if (!p) return;
    size_t usable = malloc_usable_size(p);
    __sync_sub_and_fetch(&g_live_bytes, usable);
    uint64_t w; int s = track_take(p,&w);
    if (s>=0 && g_sites[s].live_bytes>=w) g_sites[s].live_bytes-=w;
}

static void dump(int sig){
    (void)sig;
    int order[NSITES]; for(int i=0;i<g_nsites;i++) order[i]=i;
    for(int i=0;i<g_nsites;i++)for(int j=i+1;j<g_nsites;j++) if(g_sites[order[j]].live_bytes>g_sites[order[i]].live_bytes){int t=order[i];order[i]=order[j];order[j]=t;}
    fprintf(stderr,"\n=== MEMPROF live=%.1f MB peak=%.1f MB (sample 1/%llu) top sites: ===\n",
            g_live_bytes/1048576.0, g_peak_bytes/1048576.0,(unsigned long long)g_sample_every);
    for(int k=0;k<12 && k<g_nsites;k++){ struct Site* s=&g_sites[order[k]];
        if(s->live_bytes==0) continue;
        fprintf(stderr,"[#%d] live=%.1f MB samples=%llu\n",k,s->live_bytes/1048576.0,(unsigned long long)s->samples);
        backtrace_symbols_fd(s->stk, s->n, 2);
        fprintf(stderr,"----\n");
    }
    fflush(stderr);
}

__attribute__((constructor)) static void init(void){
    real_malloc=dlsym(RTLD_NEXT,"malloc"); real_calloc=dlsym(RTLD_NEXT,"calloc");
    real_realloc=dlsym(RTLD_NEXT,"realloc"); real_free=dlsym(RTLD_NEXT,"free");
    const char* e=getenv("MEMPROF_EVERY"); if(e) g_sample_every=strtoull(e,0,10); if(!g_sample_every) g_sample_every=1;
    signal(SIGUSR1,dump);
}
__attribute__((destructor)) static void fini(void){ dump(0); }

void* malloc(size_t n){ if(!real_malloc) return NULL; void* p=real_malloc(n); on_alloc(p,n); return p; }
void  free(void* p){ if(!real_free){return;} on_free(p); real_free(p); }
void* calloc(size_t a,size_t b){ if(!real_calloc){return NULL;} void* p=real_calloc(a,b); on_alloc(p,a*b); return p; }
void* realloc(void* q,size_t n){ if(!real_realloc) return NULL; if(q) on_free(q); void* p=real_realloc(q,n); on_alloc(p,n); return p; }
