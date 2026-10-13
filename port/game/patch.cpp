/*
 * The licence, and the four pointers it locks.
 *
 * Game.nativeInit() dies on its ninth instruction group with a store through a
 * null pointer:
 *
 *     14c8934  ldr r3, [r8, r3]       ; GOT -> &lockPointer4
 *     14c8940  ldr r3, [r3]           ; lockPointer4 == NULL
 *     14c8950  str r2, [r3]           ; SIGSEGV at 0x00000000
 *
 * The GOT entry relocates to 0x01f78afc, which the library's own symbol table
 * calls `lockPointer4`; its three siblings sit directly below it at 0x1f78af0,
 * 0x1f78af4 and 0x1f78af8. The same scheme as N.O.V.A. 3, and the text below
 * is that port's.
 *
 * On Android they are allocated by the installer's own native init - the
 * GameInstaller.initNative / GDRMPolicy.initNativeAP entry points, which the
 * Java installer activity calls before the game starts. That activity is a
 * licence check against a server that has been gone for years, so this port
 * does not run it. What it leaves behind is four null pointers that the engine
 * writes to without checking.
 *
 * Giving them storage is the whole fix, and it is deliberately the *smallest*
 * one: nothing here is patched, no branch is rewritten, no verdict is forged.
 * The engine writes its lock words into four words of ours and reads back what
 * it wrote.
 */

#include <stdint.h>
#include <stdio.h>

#include "so_util.h"
#include "trace.h"

#include "wb.h"

/*
 * One word per pointer, and they are separate rather than an array because the
 * engine compares the addresses in at least one place ("lockPointer" appears
 * four times in the symbol table, not once with an index) - four words of one
 * array would still be four distinct addresses, but keeping them named makes a
 * later fault in one of them attributable to that one.
 */
static int g_lock_word[4];

void wb_unlock_drm(so_module *mod)
{
    static const char *const kNames[4] = {
        "lockPointer1", "lockPointer2", "lockPointer3", "lockPointer4",
    };

    for (int i = 0; i < 4; i++) {
        int **slot = (int **)so_symbol(mod, kNames[i]);
        if (!slot) {
            trace("DRM: %s is not exported by this build - if init() faults on "
                  "a null store, this is why", kNames[i]);
            continue;
        }
        g_lock_word[i] = 0;
        *slot = &g_lock_word[i];
    }

    trace("DRM: the four GDRM lock words point at loader storage");
}

/*
 * The library's private stdio.
 *
 * libUnrealEngine3.so carries CrystaX's wide-character libc, and with it a
 * second copy of the byte-oriented stdio entry points: fopen, fdopen, fclose,
 * fread, fwrite, fputs, fputc, puts, fflush, fseek(o), ftell(o), rewind, fgets,
 * clearerr, ungetc are all defined in the module (0x1a0ebc0 onwards) rather
 * than imported. They build their FILE through bionic's own __sfp() and fill in
 * its fields, while everything the module still imports (fprintf, getc, putc,
 * ferror, setvbuf, fgetpos...) arrives at this loader's bridge, whose FILE is a
 * handle on a host stream. One file would be opened by one family and read by
 * the other.
 *
 * So the private entry points are pointed at the same bridge functions an
 * import of that name gets. The wide-character functions and the internals
 * (__sfvwrite, __vfwprintf, __srefill...) are left alone: vswprintf and
 * swscanf run them on string buffers of their own and never meet a real file.
 */
void wb_bridge_private_stdio(so_module *mod)
{
    static const char *const kNames[] = {
        "fopen", "fdopen", "fclose", "fread", "fwrite", "fputs", "fputc", "puts", "fflush",
        "fseek", "fseeko", "ftell", "ftello", "rewind", "fgets", "clearerr", "ungetc",
    };
    int hooked = 0;
    for (unsigned i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        uintptr_t impl = so_resolve_link(mod, kNames[i]);
        if (!so_symbol(mod, kNames[i]))
            continue;
        if (!impl) {
            trace("stdio: the module's own %s() has no bridge to go to - left as it is", kNames[i]);
            continue;
        }
        hook_symbol(mod, kNames[i], impl, 1);
        hooked++;
    }
    trace("stdio: %d of the module's own stdio functions now go to the loader's", hooked);
}
