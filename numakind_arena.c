/*
 * Copyright (C) 2014 Intel Corperation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice(s),
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice(s),
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <numa.h>
#include <numaif.h>
#include <jemalloc/jemalloc.h>
#define _GNU_SOURCE
#include <utmpx.h>

#include "numakind.h"
#include "numakind_arena.h"

enum {NUMAKIND_CMD_LEN = 128};

int numakind_arena_create(struct numakind *kind, const struct numakind_ops *ops, const char *name)
{
    int err = 0;
    int i;
    size_t unsigned_size = sizeof(unsigned int);

    kind->ops = ops;
    kind->name = je_malloc(strlen(name) + 1);
    if (!kind->name) {
        err = NUMAKIND_ERROR_MALLOC;
    }
    if (!err) {
        strcpy(kind->name, name);
        if (kind->ops->get_arena == numakind_cpu_get_arena) {
            kind->arena_map_len = numa_num_configured_cpus();
            kind->arena_map = (unsigned int *)je_malloc(sizeof(unsigned int) * kind->arena_map_len);
        }
        else if (kind->ops->get_arena == numakind_bijective_get_arena) {
            kind->arena_map_len = 1;
            kind->arena_map = (unsigned int *)je_malloc(sizeof(unsigned int));
        }
        else {
            kind->arena_map_len = 0;
            kind->arena_map = NULL;
        }
    }
    if (!err) {
        if (kind->arena_map_len && kind->arena_map == NULL) {
            je_free(kind->name);
            err = NUMAKIND_ERROR_MALLOC;
        }
    }
    if (!err) {
        for (i = 0; !err && i < kind->arena_map_len; ++i) {
            err = je_mallctl("arenas.extendk", kind->arena_map + i,
                             &unsigned_size, &(kind->partition),
                             unsigned_size);
        }
        if (err) {
            je_free(kind->name);
            if (kind->arena_map) {
                je_free(kind->arena_map);
            }
            err = NUMAKIND_ERROR_MALLCTL;
        }
    }
    return err;
}

int numakind_arena_destroy(struct numakind *kind)
{
    char cmd[NUMAKIND_CMD_LEN] = {0};
    int i;

    if (kind->arena_map) {
        for (i = 0; i < kind->arena_map_len; ++i) {
            snprintf(cmd, NUMAKIND_CMD_LEN, "arena.%u.purge", kind->arena_map[i]);
            je_mallctl(cmd, NULL, NULL, NULL, 0);
        }
        je_free(kind->arena_map);
        kind->arena_map = NULL;
    }
    if (kind->name) {
        je_free(kind->name);
        kind->name = NULL;
    }
    return 0;
}

void *numakind_arena_malloc(struct numakind *kind, size_t size)
{
    void *result = NULL;
    int err = 0;
    unsigned int arena;

    err = kind->ops->get_arena(kind, &arena);
    if (!err) {
        result = je_mallocx(size, MALLOCX_ARENA(arena));
    }
    return result;
}

void *numakind_arena_realloc(struct numakind *kind, void *ptr, size_t size)
{
    int err = 0;
    unsigned int arena;

    if (size == 0 && ptr != NULL) {
        numakind_free(kind, ptr);
        ptr = NULL;
    }
    else {
        err = kind->ops->get_arena(kind, &arena);
        if (!err) {
            if (ptr == NULL) {
                ptr = je_mallocx(size, MALLOCX_ARENA(arena));
            }
            else {
                ptr = je_rallocx(ptr, size, MALLOCX_ARENA(arena));
            }
        }
    }
    return ptr;
}

void *numakind_arena_calloc(struct numakind *kind, size_t num, size_t size)
{
    void *result = NULL;
    int err = 0;
    unsigned int arena;

    err = kind->ops->get_arena(kind, &arena);
    if (!err) {
        result = je_mallocx(num * size, MALLOCX_ARENA(arena) | MALLOCX_ZERO);
    }
    return result;
}

int numakind_arena_posix_memalign(struct numakind *kind, void **memptr, size_t alignment,
                                  size_t size)
{
    int err = 0;
    unsigned int arena;

    *memptr = NULL;
    err = kind->ops->get_arena(kind, &arena);
    if (!err) {
        if ( (alignment < sizeof(void*)) ||
             (((alignment - 1) & alignment) != 0) ) {
            err = NUMAKIND_ERROR_ALIGNMENT;
        }
    }
    if (!err) {
        *memptr = je_mallocx(size, MALLOCX_ALIGN(alignment) | MALLOCX_ARENA(arena));
        err = *memptr ? 0 : NUMAKIND_ERROR_MALLOCX;
    }
    return err;
}

void numakind_arena_free(struct numakind *kind, void *ptr)
{
    je_free(ptr);
}

int numakind_cpu_get_arena(struct numakind *kind, unsigned int *arena)
{
    int err = 0;
    int cpu_id;

    cpu_id = sched_getcpu();
    if (cpu_id < kind->arena_map_len) {
        *arena = kind->arena_map[cpu_id];
    }
    else {
        err = NUMAKIND_ERROR_GETCPU;
    }
    return err;
}

int numakind_bijective_get_arena(struct numakind *kind, unsigned int *arena)
{
    int err = 0;

    if (kind->arena_map != NULL) {
        *arena = *(kind->arena_map);
    }
    else {
        err = NUMAKIND_ERROR_RUNTIME;
    }
    return err;
}
