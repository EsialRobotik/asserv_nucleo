#include "ch.h"
#include "hal.h"
#include "shell.h"
#include <chprintf.h>
#include <stdlib.h>
#include <string.h>

/**
 * Permet au débugger de fonctionner et d'avor les assertions qui sortent dans la consele
 * Voir les pages suivantes pour plus d'infos :
 * 	   http://www.chibios.com/forum/viewtopic.php?f=16&t=2506#p20099
 * 	   http://www.chibios.com/forum/viewtopic.php?f=3&t=1305&start=20#p28854
 */

/**
 * Executes the BKPT instruction that causes the debugger to stop.
 * If no debugger is attached, this will be ignored
 */
#define bkpt() __asm volatile("BKPT #0\n")

void NMI_Handler(void) {
    //TODO
    while (1) {
    }
}

//See http://infocenter.arm.com/help/topic/com.arm.doc.dui0552a/BABBGBEC.html
typedef enum {
    Reset      = 1,
    NMI        = 2,
    HardFault  = 3,
    MemManage  = 4,
    BusFault   = 5,
    UsageFault = 6,
} FaultType;

void HardFault_Handler(void) {
    //Copy to local variables (not pointers) to allow GDB "i loc" to directly show the info
    //Get thread context. Contains main registers including PC and LR
    struct port_extctx ctx;
    memcpy(&ctx, (void*)__get_PSP(), sizeof(struct port_extctx));
    (void)ctx;

    //Interrupt status register: Which interrupt have we encountered, e.g. HardFault?
    volatile FaultType faultType = (FaultType)__get_IPSR();
    (void)faultType;

    SCB_Type* _SCB = SCB;

    //For HardFault/BusFault this is the address that was accessed causing the error
    uint32_t faultAddress = _SCB->BFAR;
    (void)faultAddress;

    //Flags about hardfault / busfault
    //See http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Cihdjcfc.html for reference
    volatile bool isFaultPrecise      = (((_SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 1)) ? true : false);
    volatile bool isFaultImprecise    = (((_SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 2)) ? true : false);
    volatile bool isFaultOnUnstacking = (((_SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 3)) ? true : false);
    volatile bool isFaultOnStacking   = (((_SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 4)) ? true : false);
    volatile bool isFaultAddressValid = (((_SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 7)) ? true : false);
    (void)isFaultPrecise;
    (void)isFaultImprecise;
    (void)isFaultOnUnstacking;
    (void)isFaultOnStacking;
    (void)isFaultAddressValid;

    //Cause debugger to stop. Ignored if no debugger is attached
    bkpt();
    NVIC_SystemReset();
}

void BusFault_Handler(void) __attribute__((alias("HardFault_Handler")));

void UsageFault_Handler(void) {
    //Copy to local variables (not pointers) to allow GDB "i loc" to directly show the info
    //Get thread context. Contains main registers including PC and LR
    struct port_extctx ctx;
    memcpy(&ctx, (void*)__get_PSP(), sizeof(struct port_extctx));
    (void)ctx;

    //Interrupt status register: Which interrupt have we encountered, e.g. HardFault?
    volatile FaultType faultType = (FaultType)__get_IPSR();
    (void)faultType;

    SCB_Type* _SCB = SCB;

    //Flags about hardfault / busfault
    //See http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Cihdjcfc.html for reference
    volatile bool isUndefinedInstructionFault = (((_SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 0)) ? true : false);
    volatile bool isEPSRUsageFault            = (((_SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 1)) ? true : false);
    volatile bool isInvalidPCFault            = (((_SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 2)) ? true : false);
    volatile bool isNoCoprocessorFault        = (((_SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 3)) ? true : false);
    volatile bool isUnalignedAccessFault      = (((_SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 8)) ? true : false);
    volatile bool isDivideByZeroFault         = (((_SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 9)) ? true : false);
    (void)isUndefinedInstructionFault;
    (void)isEPSRUsageFault;
    (void)isInvalidPCFault;
    (void)isNoCoprocessorFault;
    (void)isUnalignedAccessFault;
    (void)isDivideByZeroFault;

    //Cause debugger to stop. Ignored if no debugger is attached
    bkpt();
    NVIC_SystemReset();
}

void MemManage_Handler(void) {
    //Copy to local variables (not pointers) to allow GDB "i loc" to directly show the info
    //Get thread context. Contains main registers including PC and LR
    struct port_extctx ctx;
    memcpy(&ctx, (void*)__get_PSP(), sizeof(struct port_extctx));
    (void)ctx;

    //Interrupt status register: Which interrupt have we encountered, e.g. HardFault?
    volatile FaultType faultType = (FaultType)__get_IPSR();
    (void)faultType;

    SCB_Type* _SCB = SCB;

    //For HardFault/BusFault this is the address that was accessed causing the error
    uint32_t faultAddress = _SCB->MMFAR;
    (void)faultAddress;

    //Flags about hardfault / busfault
    //See http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Cihdjcfc.html for reference
    volatile bool isInstructionAccessViolation = (((_SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 0)) ? true : false);
    volatile bool isDataAccessViolation        = (((_SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 1)) ? true : false);
    volatile bool isExceptionUnstackingFault   = (((_SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 3)) ? true : false);
    volatile bool isExceptionStackingFault     = (((_SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 4)) ? true : false);
    volatile bool isFaultAddressValid          = (((_SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 7)) ? true : false);
    (void)isInstructionAccessViolation;
    (void)isDataAccessViolation;
    (void)isExceptionUnstackingFault;
    (void)isExceptionStackingFault;
    (void)isFaultAddressValid;

    //Cause debugger to stop. Ignored if no debugger is attached
    bkpt();
    NVIC_SystemReset();
}

extern BaseSequentialStream *outputStream;
static mutex_t mutex;

void dbg_assert(const char* const assertion, const char* const file, const unsigned line, const char* const func, const char* const reason) {
    chMtxObjectInit(&mutex);
	chSysUnconditionalLock();
	chMtxLockS(&mutex);
	chSysUnconditionalUnlock();

    CH_CFG_SYSTEM_HALT_HOOK(reason);

    // Let some time to the uart device to clear its buffer
	chThdSleep(chTimeMS2I(5));
    chprintf(outputStream, "%s:%i (%s): assertion (%s) failed ; reason = %s", file, line, func, assertion, reason);
	chThdSleep(chTimeMS2I(5));

    //Cause debugger to stop. Ignored if no debugger is attached
    bkpt();

    chSysHalt(func);
}

// Permet aussi de lier le assert de la libc a l'assert de chibiOs
void __assert_func(const char * assertion, const char * file, unsigned int line, const char * function)
{
	dbg_assert(assertion, file, line, function, "\n");
}

/*
 * Gros hack des enfers !
 *  Je ne sais pas pourquoi/comment, mais à un moment j'ai eu cette erreur au link :
 *  	libg.a(lib_a-fini.o): In function `__libc_fini_array':
 *			fini.c:(.text.__libc_fini_array+0x26): undefined reference to `_fini'
 *
 *	C'est une vieille étiquette obsolète, donc je ne sais pas pourquoi on se trimbale ça..
 *	 Et Giovanni il est d'accord avec moi en plus ==> http://www.chibios.com/forum/viewtopic.php?t=4172
 */
__attribute__((used))
void _fini(void)
{
}
