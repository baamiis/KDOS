#ifndef KTOS_MULTI_H_INCLUDED
#define KTOS_MULTI_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef DEBUG
  #define DEBUG                    0
#endif

typedef unsigned short  WORD;
typedef long            LONG;
typedef int             INT;
typedef unsigned char   BYTE;

#define TRUE  1
#define FALSE 0

void ktos_Emergency(const char *Msg);
void ktos_DebugPrintf(const char *Format, ...);
void ktos_InitSys(void);

#endif /* KTOS_MULTI_H_INCLUDED */