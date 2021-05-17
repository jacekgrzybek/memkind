// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (C) 2014 - 2021 Intel Corporation. */

#include "../memkind_memtier.h"

#include "common.h"
#include "config.h"
#include "decorator_memtier_test.h"




TEST()
{
    bool check = false;
    #ifdef MEMTIER_DECORATION_ENABLED
        printf("Flag works\n");
        check = true;
    #endif
    ASSERT_TRUE(check == true);
}