/* Copyright 2000-2004 The Apache Software Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "test_apr.h"
#include "apr_shm.h"
#include "apr_errno.h"
#include "apr_general.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_thread_proc.h"
#include "apr_time.h"
#include "testshm.h"
#include "apr.h"

#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if APR_HAS_SHARED_MEMORY

static int msgwait(int sleep_sec, int first_box, int last_box)
{
    int i;
    int recvd = 0;
    apr_time_t start = apr_time_now();
    apr_interval_time_t sleep_duration = apr_time_from_sec(sleep_sec);
    while (apr_time_now() - start < sleep_duration) {
        for (i = first_box; i < last_box; i++) {
            if (boxes[i].msgavail && !strcmp(boxes[i].msg, MSG)) {
                recvd++;
                boxes[i].msgavail = 0; /* reset back to 0 */
                /* reset the msg field.  1024 is a magic number and it should
                 * be a macro, but I am being lazy.
                 */
                memset(boxes[i].msg, 0, 1024);
            }
        }
        apr_sleep(apr_time_make(0, 10000)); /* 10ms */
    }
    return recvd;
}

static void msgput(int boxnum, char *msg)
{
    apr_cpystrn(boxes[boxnum].msg, msg, strlen(msg) + 1);
    boxes[boxnum].msgavail = 1;
}

static void test_anon_create(CuTest *tc)
{
    apr_status_t rv;
    apr_shm_t *shm = NULL;

    rv = apr_shm_create(&shm, SHARED_SIZE, NULL, p);
    apr_assert_success(tc, "Error allocating shared memory block", rv);
    CuAssertPtrNotNull(tc, shm);

    rv = apr_shm_destroy(shm);
    apr_assert_success(tc, "Error destroying shared memory block", rv);
}

static void test_check_size(CuTest *tc)
{
    apr_status_t rv;
    apr_shm_t *shm = NULL;
    apr_size_t retsize;

    rv = apr_shm_create(&shm, SHARED_SIZE, NULL, p);
    apr_assert_success(tc, "Error allocating shared memory block", rv);
    CuAssertPtrNotNull(tc, shm);

    retsize = apr_shm_size_get(shm);
    CuAssertIntEquals(tc, SHARED_SIZE, retsize);

    rv = apr_shm_destroy(shm);
    apr_assert_success(tc, "Error destroying shared memory block", rv);
}

static void test_shm_allocate(CuTest *tc)
{
    apr_status_t rv;
    apr_shm_t *shm = NULL;

    rv = apr_shm_create(&shm, SHARED_SIZE, NULL, p);
    apr_assert_success(tc, "Error allocating shared memory block", rv);
    CuAssertPtrNotNull(tc, shm);

    boxes = apr_shm_baseaddr_get(shm);
    CuAssertPtrNotNull(tc, boxes);

    rv = apr_shm_destroy(shm);
    apr_assert_success(tc, "Error destroying shared memory block", rv);
}

#if APR_HAS_FORK
static void test_anon(CuTest *tc)
{
    apr_proc_t proc;
    apr_status_t rv;
    apr_shm_t *shm;
    apr_size_t retsize;
    int cnt, i;
    int recvd;

    rv = apr_shm_create(&shm, SHARED_SIZE, NULL, p);
    apr_assert_success(tc, "Error allocating shared memory block", rv);
    CuAssertPtrNotNull(tc, shm);

    retsize = apr_shm_size_get(shm);
    CuAssertIntEquals(tc, SHARED_SIZE, retsize);

    boxes = apr_shm_baseaddr_get(shm);
    CuAssertPtrNotNull(tc, boxes);

    rv = apr_proc_fork(&proc, p);
    if (rv == APR_INCHILD) { /* child */
        int num = msgwait(5, 0, N_BOXES);
        /* exit with the number of messages received so that the parent
         * can check that all messages were received.
         */
        exit(num);
    }
    else if (rv == APR_INPARENT) { /* parent */
        i = N_BOXES;
        cnt = 0;
        while (cnt++ < N_MESSAGES) {
            if ((i-=3) < 0) {
                i += N_BOXES; /* start over at the top */
            }
            msgput(i, MSG);
            apr_sleep(apr_time_make(0, 10000));
        }
    }
    else {
        CuFail(tc, "apr_proc_fork failed");
    }
    /* wait for the child */
    rv = apr_proc_wait(&proc, &recvd, NULL, APR_WAIT);
    CuAssertIntEquals(tc, N_MESSAGES, recvd);

    rv = apr_shm_destroy(shm);
    apr_assert_success(tc, "Error destroying shared memory block", rv);
}
#endif

static void test_named(CuTest *tc)
{
    apr_status_t rv;
    apr_shm_t *shm = NULL;
    apr_size_t retsize;
    apr_proc_t pidproducer, pidconsumer;
    apr_procattr_t *attr1 = NULL, *attr2 = NULL;
    int sent, received;
    apr_exit_why_e why;
    const char *args[4];

    rv = apr_shm_create(&shm, SHARED_SIZE, SHARED_FILENAME, p);
    apr_assert_success(tc, "Error allocating shared memory block", rv);
    CuAssertPtrNotNull(tc, shm);

    retsize = apr_shm_size_get(shm);
    CuAssertIntEquals(tc, SHARED_SIZE, retsize);

    boxes = apr_shm_baseaddr_get(shm);
    CuAssertPtrNotNull(tc, boxes);

    rv = apr_procattr_create(&attr1, p);
    CuAssertPtrNotNull(tc, attr1);
    apr_assert_success(tc, "Couldn't create attr1", rv);
    args[0] = apr_pstrdup(p, "testshmproducer" EXTENSION);
    args[1] = NULL;
    rv = apr_proc_create(&pidproducer, "./testshmproducer" EXTENSION, args,
                         NULL, attr1, p);
    apr_assert_success(tc, "Couldn't launch producer", rv);

    rv = apr_procattr_create(&attr2, p);
    CuAssertPtrNotNull(tc, attr2);
    apr_assert_success(tc, "Couldn't create attr2", rv);
    args[0] = apr_pstrdup(p, "testshmconsumer" EXTENSION);
    rv = apr_proc_create(&pidconsumer, "./testshmconsumer" EXTENSION, args, 
                         NULL, attr2, p);
    apr_assert_success(tc, "Couldn't launch consumer", rv);

    rv = apr_proc_wait(&pidconsumer, &received, &why, APR_WAIT);
    CuAssertIntEquals(tc, APR_CHILD_DONE, rv);
    CuAssertIntEquals(tc, APR_PROC_EXIT, why);

    rv = apr_proc_wait(&pidproducer, &sent, &why, APR_WAIT);
    CuAssertIntEquals(tc, APR_CHILD_DONE, rv);
    CuAssertIntEquals(tc, APR_PROC_EXIT, why);

    /* Cleanup before testing that producer and consumer worked correctly.
     * This way, if they didn't succeed, we can just run this test again
     * without having to cleanup manually.
     */
    apr_assert_success(tc, "Error destroying shared memory", 
                       apr_shm_destroy(shm));
    
    CuAssertIntEquals(tc, sent, received);

}
#endif

CuSuite *testshm(void)
{
    CuSuite *suite = CuSuiteNew("Shared Memory");

#if APR_HAS_SHARED_MEMORY
    SUITE_ADD_TEST(suite, test_anon_create);
    SUITE_ADD_TEST(suite, test_check_size);
    SUITE_ADD_TEST(suite, test_shm_allocate);
#if APR_HAS_FORK
    SUITE_ADD_TEST(suite, test_anon);
#endif
    SUITE_ADD_TEST(suite, test_named);
#endif

    return suite;
}


