# created by Davide Airaghi for FPU-state handling

#if !defined(_FPU_H)

#define FPU_STATE_LEN 108

typedef struct {
    char state[FPU_STATE_LEN];
} __attribute__((packed)) fpu_state;

#define FPU_STATUS_SAVE(fstat)  asm volatile("fsave %0\n\t" : "=m" (*(fstat.state)));

#define FPU_STATUS_RESTORE(fstat) __asm__("frstor %0\n\t" : "=m" (*(fstat.state)));     

#define FPU_STATUS_ZERO(fstat,i_fpu) for (i_fpu=0;i_fpu<FPU_STATE_LEN;i_fpu++) fstat.state[i_fpu] = '\0';

#define _FPU_H
#endif
