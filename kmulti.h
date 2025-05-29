#ifndef KMULTI_H_INCLUDED
#define KMULTI_H_INCLUDED

#ifndef DEBUG
  #define DEBUG                    0
#endif

void Emergency(const char *Msg);
void DebugPrintf(const char *Format, ...);
void InitSys(void);

#endif /* KMULTI_H_INCLUDED */