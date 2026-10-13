/*
 * Engine tweaks that do not depend on offsets inside the library.
 *
 *   <PREFIX>_FAST_CAS=0   turn off the atomics fix below (on by default). A library built for ARMv5
 *                         calls the kernel's cmpxchg helper at 0xffff0fc0 for its __sync_* functions,
 *                         which qemu-user emulates by trapping out of translated code on every call.
 *                         Each literal holding that address is pointed at a small ldrex/strex routine.
 *   <PREFIX>_UCLAMP=0     leave the scheduler's clamp on the main thread alone (see wb_sched_tick).
 *   <PREFIX>_UCLAMP_MIN=0..1024 also ask for at least this much (0 by default).
 */
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "so_util.h"
#include "trace.h"
#include "port_env.h"

#include "wb.h"

static void install_fast_cas(so_module *mod)
{
    /* kuser cmpxchg: r0 = old, r1 = new, r2 = ptr; returns r0 = 0 when it stored. */
    static const uint32_t code[] = {
        0xf57ff05b, /* dmb ish            */
        0xe1923f9f, /* 1: ldrex r3,[r2]   */
        0xe0533000, /* subs r3,r3,r0      */
        0x01823f91, /* strexeq r3,r1,[r2] */
        0x03330001, /* teqeq r3,#1        */
        0x0afffffa, /* beq 1b             */
        0xf57ff05b, /* dmb ish            */
        0xe1a00003, /* mov r0,r3          */
        0xe12fff1e, /* bx lr              */
    };
    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        trace("tweaks: fast cas: mmap failed");
        return;
    }
    memcpy(page, code, sizeof(code));
    uint32_t *w = (uint32_t *)mod->text_base;
    size_t n = mod->text_size / 4, hits = 0;
    for (size_t i = 0; i < n; i++) {
        if (w[i] == 0xffff0fc0u) {
            w[i] = (uint32_t)(uintptr_t)page;
            hits++;
        }
    }
    trace("tweaks: kuser cmpxchg literal replaced at %zu places (routine %p)", hits, page);
}

void wb_tweaks_install(so_module *mod)
{
    if (port_getenv_long("FAST_CAS", 1))
        install_fast_cas(mod);
}

/*
 * The phone's scheduler puts a utilisation clamp on the game's main thread a few seconds into a
 * mission: uclamp.max 171-199 of 1024, on that one thread and no other. To the frequency governor the
 * thread then never looks more than a fifth loaded, and the core stays at its lowest clock while the
 * thread is in fact saturated (util 1023, 1.0 GHz of 4.3). The clamp is a per-task attribute its owner
 * may set, so the main thread sets it back - again and again, because whatever lowers it does so
 * repeatedly.
 *
 * sched_setattr is not something Android apps normally call, and a system call the seccomp filter
 * does not know kills the process, so the first call is made in a forked child.
 */
struct wb_sched_attr {
    uint32_t size, sched_policy;
    uint64_t sched_flags;
    int32_t  sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime, sched_deadline, sched_period;
    uint32_t sched_util_min, sched_util_max;
};

static int set_uclamp(uint32_t lo)
{
    wb_sched_attr a;
    memset(&a, 0, sizeof(a));
    a.size = sizeof(a);
    a.sched_flags = 0x08 /* KEEP_POLICY */ | 0x10 /* KEEP_PARAMS */ | 0x20 /* UTIL_CLAMP_MIN */ | 0x40 /* UTIL_CLAMP_MAX */;
    a.sched_util_min = lo;
    a.sched_util_max = 1024;
    return syscall(SYS_sched_setattr, 0, &a, 0) == 0 ? 0 : errno;
}

void wb_sched_tick(long frames)
{
    static int state = -1;      /* -1 not tried, 0 off, 1 working */
    static uint32_t lo;

    if (state == 0 || (state == 1 && frames % 8 != 0))
        return;
    if (state < 0) {
        state = 0;
        if (!port_getenv_long("UCLAMP", 1))
            return;
        long v = port_getenv_long("UCLAMP_MIN", 0);
        lo = v < 0 ? 0 : v > 1024 ? 1024 : (uint32_t)v;
        pid_t child = fork();
        if (child < 0)
            return;
        if (child == 0)
            _exit(set_uclamp(lo) & 0x7f);
        int st = 0;
        if (waitpid(child, &st, 0) != child || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            trace("tweaks: uclamp: sched_setattr unusable here (%s %d) - left alone",
                  WIFEXITED(st) ? "errno" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
            return;
        }
        state = 1;
        trace("tweaks: uclamp: main thread kept at min %u, max 1024", lo);
    }
    int e = set_uclamp(lo);
    if (e) {
        trace("tweaks: uclamp: sched_setattr failed (errno %d) - stopped", e);
        state = 0;
    }
}
