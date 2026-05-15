/**
 * @file alloc.c
 * @brief Minimal bump allocator — CALL0 ABI replacement for SDK malloc/calloc.
 *
 * The ESP8266 SDK's malloc/calloc are compiled with windowed ABI and
 * cannot be called safely from -mabi=call0 code (SP corruption via RETW).
 * This file provides CALL0-safe alternatives that the linker will prefer
 * over the SDK versions.
 *
 * Trade-off: free() is a no-op.  KTOS calls calloc() a handful of times
 * at startup (TCBs, stacks, message queues) and never frees them, so
 * this is perfectly adequate.
 *
 * Compile flag: -mabi=call0
 */

#include <stddef.h>
#include <stdint.h>

/* 8 KB static heap — increase if you add more tasks or larger stacks. */
#define HEAP_SIZE 8192

static uint8_t  g_heap[HEAP_SIZE] __attribute__((aligned(4)));
static uint32_t g_used;

void *malloc(size_t size)
{
    size = (size + 3u) & ~3u;           /* 4-byte align */
    if (g_used + size > HEAP_SIZE) return NULL;
    void *p = g_heap + g_used;
    g_used += (uint32_t)size;
    return p;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    uint8_t *p = (uint8_t *)malloc(total);
    if (p) {
        for (size_t i = 0; i < total; i++) p[i] = 0;
    }
    return (void *)p;
}

/* No-op: KTOS never frees its task/stack/queue memory after init. */
void free(void *ptr) { (void)ptr; }
