// bsp_mycpu.c
// Board Support Package implementations for KDOS HAL

#include "k_hal.h"
#include "kdos.h" // May be needed for WORD, LONG, TaskCurrent, OS_SP etc. if used in HAL impl.
                  // Or include a specific types header if you have one.

// Include your microcontroller's specific register definition header here
// For example:
// #include "stm32f4xx.h"
// #include "atmega328p.h"

// --- Global Variables for BSP (if any) ---
// Example: you might need to store the OS_SP if K_HAL_StartScheduler doesn't directly use the global
// static void* g_bsp_os_stack_pointer = NULL;

// --- Interrupt Control ---

void K_HAL_DisableInterrupts(void)
{
    // TODO: Implement architecture-specific code to disable all relevant interrupts.
    // This might involve:
    //   - Setting a bit in a global interrupt enable register (e.g., CPSID I on ARM Cortex-M).
    //   - Saving the previous interrupt state if you plan to support nested critical sections
    //     (though for KDOS's current design, a simple global disable/enable is likely sufficient).
    //
    // Example (ARM Cortex-M):
    //   __asm volatile ("cpsid i" : : : "memory");
}

void K_HAL_EnableInterrupts(void)
{
    // TODO: Implement architecture-specific code to re-enable interrupts.
    // This might involve:
    //   - Clearing a bit in a global interrupt enable register (e.g., CPSIE I on ARM Cortex-M).
    //   - Restoring a previously saved interrupt state if implementing nested critical sections.
    //
    // Example (ARM Cortex-M):
    //   __asm volatile ("cpsie i" : : : "memory");
}

// --- Context Switching & Task Initialization ---

void *K_HAL_InitTaskStack(void *p_stack_base,
                          unsigned int stack_size_bytes,
                          void (*task_func_addr)(WORD, WORD, LONG),
                          void (*task_exit_handler_addr)(WORD),
                          WORD initial_msg_type,
                          WORD initial_sparam,
                          LONG initial_lparam)
{
    // TODO: Implement this function to set up the initial stack frame for a new task.
    // This is highly architecture-specific and usually involves some assembly.
    //
    // What this function needs to do:
    // 1. Calculate the actual top-of-stack (TOS) pointer. Stacks usually grow downwards.
    //    So, TOS would be something like `(char*)p_stack_base + stack_size_bytes`.
    //    Ensure the stack pointer is correctly aligned for the CPU architecture.
    //
    // 2. "Push" initial CPU register values onto this stack in the order the CPU expects
    //    them for a context restore. This includes:
    //    - A Program Status Register (PSR) with appropriate mode/interrupt settings.
    //    - The Program Counter (PC) set to `task_func_addr`.
    //    - The Link Register (LR) set to `task_exit_handler_addr`.
    //    - Initial values for general-purpose registers (often 0 or a debug pattern).
    //    - Registers for passing parameters: `initial_msg_type`, `initial_sparam`, `initial_lparam`
    //      must be placed where the `task_func_addr` will find them according to the
    //      architecture's calling convention (e.g., R0, R1, R2 on ARM).
    //
    // 3. The `task_exit_handler_addr` must be set up so that if `task_func_addr` returns,
    //    the `task_exit_handler_addr` is called, and the `WORD` return value from
    //    `task_func_addr` is passed as the first argument to `task_exit_handler_addr`.
    //    This might involve setting LR to a small assembly trampoline that moves the
    //    return value from its register (e.g., R0) into the first argument register for
    //    `task_exit_handler_addr` and then branches to `task_exit_handler_addr`.
    //
    // 4. Return the final value of the stack pointer (after all initial context is pushed).
    //    This will be the value stored in `Task->StackPtr`.
    //
    // Example (conceptual for ARM Cortex-M - actual implementation is more complex):
    //   unsigned long *stack_ptr = (unsigned long*)(((unsigned char*)p_stack_base + stack_size_bytes) & ~0x7UL); // Ensure 8-byte alignment
    //   *(--stack_ptr) = 0x01000000L; // xPSR (Thumb state)
    //   *(--stack_ptr) = (unsigned long)task_func_addr; // PC
    //   *(--stack_ptr) = (unsigned long)task_exit_handler_addr; // LR (or trampoline that calls it with task_func_addr's R0)
    //   *(--stack_ptr) = initial_lparam; // R2 (example, depends on calling convention for 3rd param)
    //   *(--stack_ptr) = initial_sparam; // R1
    //   *(--stack_ptr) = initial_msg_type; // R0
    //   *(--stack_ptr) = 0xCCCCCCCCL; // R12
    //   *(--stack_ptr) = 0xCCCCCCCCL; // R3
    //   *(--stack_ptr) = 0xCCCCCCCCL; // R2 (if not used for lparam)
    //   *(--stack_ptr) = 0xCCCCCCCCL; // R1 (if not used for sparam)
    //   *(--stack_ptr) = 0xCCCCCCCCL; // R0 (if not used for msg_type)
    //   // ... push other registers (R4-R11 often) ...
    //   return stack_ptr;

    (void)p_stack_base;
    (void)stack_size_bytes;
    (void)task_func_addr;
    (void)task_exit_handler_addr;
    (void)initial_msg_type;
    (void)initial_sparam;
    (void)initial_lparam;

    // If stack setup fails (e.g., stack_size_bytes is too small), return NULL.
    // Emergency("K_HAL_InitTaskStack: Not implemented for this BSP!");
    return NULL;
}

// Global variable to store OS_SP, accessible by assembly context switch routine if needed.
// extern int32_t* OS_SP; // OS_SP is already declared static in Kdos.c.
// The HAL might need a way to access it if K_HAL_ContextSwitch is pure assembly.
// Alternatively, OS_SP can be passed to an assembly wrapper.

void K_HAL_ContextSwitch(void **p_current_task_sp_storage, void *next_task_sp_val)
{
    // TODO: Implement the context switch. This is almost entirely assembly language.
    //
    // This function is called with interrupts DISABLED.
    //
    // Steps:
    // 1. Save CPU registers of the current context:
    //    - Push all general-purpose registers (that are not callee-saved by C convention
    //      if switching between C functions, but for OS context switch, save all GPRs).
    //    - Push the Program Status Register (PSR).
    //    - The Stack Pointer (SP) register now points to the top of this saved context.
    //
    // 2. Save the current CPU SP (which now points to the saved context on the current stack)
    //    into the location pointed to by `p_current_task_sp_storage`.
    //    Example: `*p_current_task_sp_storage = (void*)__get_PSP();` (ARM Cortex-M Process Stack Pointer)
    //             or `*p_current_task_sp_storage = (void*)__get_MSP();` (ARM Cortex-M Main Stack Pointer)
    //
    // 3. Set the CPU SP to `next_task_sp_val`.
    //    Example: `__set_PSP((uint32_t)next_task_sp_val);`
    //             or `__set_MSP((uint32_t)next_task_sp_val);`
    //
    // 4. Restore CPU registers for the new context:
    //    - Pop the Program Status Register (PSR) from the new stack.
    //    - Pop all general-purpose registers from the new stack.
    //    - The final operation is usually a special return-from-interrupt/exception instruction
    //      or a branch that loads the PC and PSR, effectively resuming the new context.
    //
    // Note on Interrupts:
    // The K_HAL_ContextSwitch is called with interrupts disabled.
    // The act of restoring the PSR for the new task will typically restore its interrupt state.
    // If the new task was previously running with interrupts enabled, they will become enabled
    // as part of restoring its PSR.

    (void)p_current_task_sp_storage;
    (void)next_task_sp_val;
    // Emergency("K_HAL_ContextSwitch: Not implemented for this BSP!");
}

void K_HAL_StartScheduler(void *first_task_stack_ptr)
{
    // TODO: Implement this to start the OS and the first task.
    // This function does not return.
    //
    // Steps:
    // 1. Ensure interrupts are disabled (caller `RunOS` should ensure this, or do it here).
    //    K_HAL_DisableInterrupts(); // If not already done by caller.
    //
    // 2. Get the current Stack Pointer. This SP belongs to the context that called RunOS
    //    (e.g., main() after C runtime setup). This will be the initial OS/scheduler stack.
    //    Store this value in the global `OS_SP` variable in `Kdos.c`.
    //    You might need an assembly intrinsic or a small asm function to get the current SP.
    //    `extern int32_t* OS_SP;` // (Need to ensure visibility if OS_SP is static in Kdos.c)
    //    `OS_SP = (int32_t*) Your_Get_Current_SP_Function();`
    //    (If OS_SP is static in Kdos.c, this HAL function might need to be in Kdos.c too,
    //     or OS_SP needs to be made non-static or accessible via a setter function).
    //     For now, assume OS_SP is globally accessible or passed to K_HAL_ContextSwitch.
    //
    // 3. Set the CPU's current Stack Pointer to `first_task_stack_ptr`.
    //
    // 4. Restore the full CPU context of the first task from its stack (which was
    //    prepared by `K_HAL_InitTaskStack`). This includes setting the PC to the
    //    task's entry point and PSR to an appropriate initial state (interrupts enabled).
    //
    // 5. This last step effectively "returns" or jumps into the first task.
    //
    // Example (Conceptual for ARM Cortex-M):
    //   K_HAL_DisableInterrupts(); // Ensure they are off
    //   void* main_stack_ptr;
    //   __asm volatile ("mov %0, sp" : "=r"(main_stack_ptr)); // Get current SP (main's stack)
    //   // somehow set the global OS_SP in Kdos.c to main_stack_ptr
    //   // For example, if OS_SP is accessible:
    //   // extern int32_t* OS_SP; // from Kdos.c
    //   // OS_SP = main_stack_ptr;
    //
    //   __asm volatile ("msr psp, %0" : : "r" (first_task_stack_ptr)); // Set Process Stack Pointer
    //   // Code to ensure CPU uses PSP for thread mode and to trigger return to thread mode using PSP.
    //   // This often involves setting CONTROL register and using BX LR with a special LR value.
    //   // Then, load R0-R3, R12, LR, PC, xPSR from first_task_stack_ptr and execute.
    //   // This is complex and highly specific.

    (void)first_task_stack_ptr;
    // Emergency("K_HAL_StartScheduler: Not implemented for this BSP!");
    // This function should loop indefinitely if the context switch part fails,
    // as KDOS expects it not to return.
    while (1)
        ;
}

// --- System Timer ---

void K_HAL_InitSystemTimer(void (*timer_isr_addr)(void))
{
    // TODO: Implement architecture-specific code to:
    // 1. Configure a hardware timer (e.g., SysTick on ARM Cortex-M, or another peripheral timer).
    // 2. Set its period to generate interrupts at the KDOS tick rate (e.g., 1ms).
    // 3. Register `timer_isr_addr` (which is `key_timer_irq_handler` from `Kdos.c`)
    //    as the Interrupt Service Routine for this timer. This means setting up the
    //    vector table entry for the timer interrupt to point to `timer_isr_addr`.
    // 4. Set the timer interrupt priority appropriately.
    // 5. Enable the timer interrupt in the interrupt controller.
    // 6. Start the timer.
    //
    // Example (Conceptual for SysTick on ARM Cortex-M):
    //   uint32_t core_clock_hz = 16000000; // Example: 16 MHz
    //   uint32_t ticks_per_ms = core_clock_hz / 1000;
    //   SysTick_Config(ticks_per_ms); // Configures SysTick for 1ms and enables it.
    //   // The SysTick_Handler (vector name) would need to be an assembly wrapper
    //   // that calls the C function `timer_isr_addr`. Or, if the vector table can
    //   // point directly to C functions and the compiler handles ISR prologue/epilogue,
    //   // you might be able to assign `timer_isr_addr` to the vector.

    (void)timer_isr_addr;
    // Emergency("K_HAL_InitSystemTimer: Not implemented for this BSP!");
}

// Optional: Define K_HAL_ISR_FUNCTION_ATTRIBUTE if your compiler needs specific attributes for ISRs
// For example, for GCC ARM:
// #define K_HAL_ISR_FUNCTION_ATTRIBUTE __attribute__((interrupt("IRQ")))
// Then, in kdos.c, key_timer_irq_handler would be declared:
// void K_HAL_ISR_FUNCTION_ATTRIBUTE key_timer_irq_handler(void) { ... }
// If no attribute is needed, this can be an empty define:
// #define K_HAL_ISR_FUNCTION_ATTRIBUTE