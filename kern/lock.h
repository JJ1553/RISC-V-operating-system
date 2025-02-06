// lock.h - A sleep lock
//
#include "intr.h"

#ifdef LOCK_TRACE
#define TRACE
#endif

#ifdef LOCK_DEBUG
#define DEBUG
#endif

#ifndef _LOCK_H_
#define _LOCK_H_

#include "thread.h"
#include "halt.h"
#include "console.h"

struct lock {
    struct condition cond;
    int tid; // thread holding lock or -1
};

static inline void lock_init(struct lock * lk, const char * name);
static inline void lock_acquire(struct lock * lk);
static inline void lock_release(struct lock * lk);

// INLINE FUNCTION DEFINITIONS
//

static inline void lock_init(struct lock * lk, const char * name) {
    trace("%s(<%s:%p>", __func__, name, lk);
    condition_init(&lk->cond, name);
    lk->tid = -1;
}

/*
    Inputs: lock * lk
    Outputs: none
    Description: This function allows a thread to acquire a lock if it is not
    already acquired. If it is already acquired by the same thread, the fxn exits.
    If it is acquired by a different thread, the current thread waits for the condition
    specified in the lock struct
*/
static inline void lock_acquire(struct lock * lk) {
    // TODO: FIXME implement this
    trace("%s(<%s:%p>", __func__, lk->cond.name, lk);
    if(lk->tid == running_thread()) {
        return;
    }
    intr_disable();
    
    // While the thread is claimed by a different thread, wait for the lock condition
    while (lk->tid != -1 && lk->tid != running_thread()) {
        condition_wait(&lk->cond);
    }
    //  Set current thread to the tid of the lock to acquire it
    lk->tid = running_thread();
}

static inline void lock_release(struct lock * lk) {
    trace("%s(<%s:%p>", __func__, lk->cond.name, lk);

    assert (lk->tid == running_thread());
    
    lk->tid = -1;
    condition_broadcast(&lk->cond);

    //console_printf("Thread <%s:%d> released lock <%s:%p> and lk->tid is %d\n",
    //    thread_name(running_thread()), running_thread(),
    //    lk->cond.name, lk, lk->tid);
}

#endif // _LOCK_H_