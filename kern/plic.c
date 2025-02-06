// plic.c - RISC-V PLIC
//

#include "plic.h"
#include "console.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

// *** Note to student: you MUST use PLIC_IOBASE for all address calculations,
// as this will be used for testing!

#ifndef PLIC_IOBASE
#define PLIC_IOBASE 0x0C000000
#endif

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 1
#define PLIC_PENDING_START 0x1000
#define PLIC_INTREN_START 0x2000
#define PLIC_INTR_SRC_SPACE 0x80
#define PLIC_THRESHOLD_START 0x200000
#define PLIC_CXT_SPACE 0x1000
#define PLIC_CLAIM_START 0x200004

// INTERNAL FUNCTION DECLARATIONS
//

// *** Note to student: the following MUST be declared extern. Do not change these
// function delcarations!

extern void plic_set_source_priority(uint32_t srcno, uint32_t level);
extern int plic_source_pending(uint32_t srcno);
extern void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_set_context_threshold(uint32_t ctxno, uint32_t level);
extern uint32_t plic_claim_context_interrupt(uint32_t ctxno);
extern void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno);

// Currently supports only single-hart operation. The low-level PLIC functions
// already understand contexts, so we only need to modify the high-level
// functions (plic_init, plic_claim, plic_complete).

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
    int i;

    // Disable all sources by setting priority to 0, enable all sources for
    // context 0 (M mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++) {
        plic_set_source_priority(i, 0);
        plic_enable_source_for_context(1, i); // Hardwired context 1 (S mode on hart 0)
    }
}

extern void plic_enable_irq(int irqno, int prio) {
    trace("%s(irqno=%d,prio=%d)", __func__, irqno, prio);
    plic_set_source_priority(irqno, prio);
}

extern void plic_disable_irq(int irqno) {
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_irq(void) {
    
    trace("%s()", __func__);
    return plic_claim_context_interrupt(1); // Hardwired context 1 (S mode on hart 0)
}

extern void plic_close_irq(int irqno) {
    
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(1, irqno); // Hardwired context 1 (S mode on hart 0)
}

// INTERNAL FUNCTION DEFINITIONS
//

void plic_set_source_priority(uint32_t srcno, uint32_t level) {
    //inputs: srcno - source number, level - priority level
    //outputs: none

    //Description: sets the priority level of the given source number to the given level

    if (srcno == 0 || srcno >= PLIC_SRCCNT) {
        return;  // Invalid source number
    }
    if (level < PLIC_PRIO_MIN || level > PLIC_PRIO_MAX) {
        return;  // Invalid priority level
    }
    volatile uint32_t *prio_lvl = (uint32_t *)((uintptr_t)PLIC_IOBASE + (0x4 * srcno)); //base plic address + 0x4 (32 bit) * srcno per plic doc
    *prio_lvl = level;
}



int plic_source_pending(uint32_t srcno) {
    //inputs: srcno - source number
    //outputs: 1 if the source is pending, 0 if not

    //Description: checks if the given source number is pending and returns 1 if it is, 0 if not


    if (srcno == 0 || srcno >= PLIC_SRCCNT) {
        return 0;  // Invalid source number
    }

    //base plic address + 0x1000 (starting pending addy) + 0x4 (32 bit) * (srcno/32) per plic doc "The pending bit for interrupt ID N is stored in bit (N mod 32) of word (N/32)"
    volatile uint32_t * pending_word = (uint32_t *)((uintptr_t)PLIC_IOBASE + PLIC_PENDING_START + (0x4 * (srcno / 32)));  

    //srcno % 32 is the bit position in the word
    uint32_t pending_bit = *pending_word & (1 << (srcno % 32));

    //return 1 if bit is set to 1, 0 if not                        
    return pending_bit != 0;    
}

void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    //inputs: ctxno - context number, srcno - source number
    //outputs: none

    //Description: enables the given source number for the given context number


    if (ctxno > 1 || srcno == 0 || srcno >= PLIC_SRCCNT) {
        return;  // Invalid context or source number
    }

    //base plic address + 0x2000 (starting enable addy) + 0x80 (gap between contexts) * ctxno + 0x4 (32 bit)* (srcno/32) per plic doc "The pending bit for interrupt ID N is stored in bit (N mod 32) of word (N/32)"
    volatile uint32_t * enable_word = (uint32_t *)((uintptr_t)PLIC_IOBASE + PLIC_INTREN_START + (PLIC_INTR_SRC_SPACE * ctxno) + (0x4 * (srcno / 32)));

    //srcno % 32 is the bit position in the word
    *enable_word |= (1 << (srcno % 32));
}

void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcid) {
    //inputs: ctxno - context number, srcno - source number
    //outputs: none

    //Description: disables the given source number for the given context number


    if (ctxno > 1 || srcid == 0 || srcid >= PLIC_SRCCNT) {
        return;  // Invalid context or source number
    }

    //base plic address + 0x2000 (starting enable addy) + 0x80 (gap between contexts) * ctxno + 0x4 (32 bit) * (srcno/32) per plic doc "The pending bit for interrupt ID N is stored in bit (N mod 32) of word (N/32)
    volatile uint32_t * enable_word = (uint32_t *)((uintptr_t)PLIC_IOBASE + PLIC_INTREN_START + (PLIC_INTR_SRC_SPACE * ctxno) + (0x4 * (srcid / 32)));
    
    //srcno % 32 is the bit position in the word
    *enable_word &= ~(1 << (srcid % 32));
}

void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
    //inputs: ctxno - context number, level - priority level
    //outputs: none

    //Description: sets the threshold level of the given context number to the given level

    if (ctxno > 1) {
        return;  // Invalid context number
    }
    if (level < PLIC_PRIO_MIN || level > PLIC_PRIO_MAX) {
        return;  // Invalid priority level
    }

    //base plic address + 0x200000 (starting threshold addy) + 0x1000 (gap between contexts) * ctxno
    volatile uint32_t * threshold = (uint32_t *)((uintptr_t)PLIC_IOBASE + PLIC_THRESHOLD_START + (PLIC_CXT_SPACE * ctxno));

    *threshold = level;
}

uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
    //inputs: ctxno - context number
    //outputs: source number of the highest priority pending interrupt

    //Description: claims the highest priority pending interrupt for the given context number

    if (ctxno > 1) {
        return 0;  // Invalid context number
    }

    //base plic address + 0x200004 (starting claim addy) + 0x1000 (gap between contexts) * ctxno
    volatile uint32_t * claim = (uint32_t *)((uintptr_t)PLIC_IOBASE + PLIC_CLAIM_START + (PLIC_CXT_SPACE * ctxno));

    return *claim;
}

void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
    //inputs: ctxno - context number, srcno - source number
    //outputs: none

    //Description: completes the interrupt for the given context number and source number

    if (ctxno > 1 || srcno == 0 || srcno >= PLIC_SRCCNT) {
        return;  // Invalid context or source number
    }

    //base plic address + 0x200004 (starting claim addy) + 0x1000 (gap between contexts) * ctxno
    volatile uint32_t * claim = (uint32_t *)((uintptr_t)PLIC_IOBASE + PLIC_CLAIM_START + (PLIC_CXT_SPACE * ctxno));

    *claim = srcno;
}