/*
 * Copyright (c) 2010-2016 Richard Geldreich, Jr. and Binomial LLC
 * Copyright (c) 2020 FrozenStorm Interactive, Yoann Potinet
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation or credits
 *    is required.
 * 
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include "crn_core.h"
#include "crn_threading_pthreads.h"
#include "crn_timer.h"

#if CRNLIB_USE_PTHREADS_API

#ifdef WIN32
#include "crn_winhdr.h"
#include <process.h>
#else
#include <unistd.h>
#if defined(CRN_OS_DARWIN)
#include "crn_darwin_pthreads.h"
#endif
#endif

namespace crnlib
{
    uint g_number_of_processors = 1;

    void crn_threading_init()
    {
#ifdef WIN32
        SYSTEM_INFO g_system_info;
        GetSystemInfo(&g_system_info);
        g_number_of_processors = math::maximum<uint>(1U, g_system_info.dwNumberOfProcessors);
#else
        g_number_of_processors = math::maximum<int>(1, sysconf(_SC_NPROCESSORS_ONLN));
#endif
    }

    crn_thread_id_t crn_get_current_thread_id()
    {
#if defined(CRN_OS_BSD4) || defined(CRN_OS_DARWIN)
        crn_thread_id_t id;
        pthread_threadid_np(pthread_self(), &id);
        return id;
#else
        return static_cast<crn_thread_id_t>(pthread_self());
#endif
    }

    void crn_sleep(unsigned int milliseconds)
    {
#ifdef WIN32
        struct timespec interval;
        interval.tv_sec = milliseconds / 1000;
        interval.tv_nsec = (milliseconds % 1000) * 1000000L;
        pthread_delay_np(&interval);
#else
        while (milliseconds)
        {
            int msecs_to_sleep = CRNLIB_MIN(milliseconds, 1000);
            usleep(msecs_to_sleep * 1000);
            milliseconds -= msecs_to_sleep;
        }
#endif
    }

    mutex::mutex(unsigned int spin_count)
    {
        spin_count;

        if (pthread_mutex_init(&m_mutex, nullptr))
        {
            crnlib_fail("mutex::mutex: pthread_mutex_init() failed", __FILE__, __LINE__);
        }

#ifdef CRNLIB_BUILD_DEBUG
        m_lock_count = 0;
#endif
    }

    mutex::~mutex()
    {
#ifdef CRNLIB_BUILD_DEBUG
        if (m_lock_count)
        {
            crnlib_assert("mutex::~mutex: mutex is still locked", __FILE__, __LINE__);
        }
#endif
        if (pthread_mutex_destroy(&m_mutex))
        {
            crnlib_assert("mutex::~mutex: pthread_mutex_destroy() failed", __FILE__, __LINE__);
        }
    }

    void mutex::lock()
    {
        pthread_mutex_lock(&m_mutex);
#ifdef CRNLIB_BUILD_DEBUG
        m_lock_count++;
#endif
    }

    void mutex::unlock()
    {
#ifdef CRNLIB_BUILD_DEBUG
        if (!m_lock_count)
        {
            crnlib_assert("mutex::unlock: mutex is not locked", __FILE__, __LINE__);
        }
        m_lock_count--;
#endif
        pthread_mutex_unlock(&m_mutex);
    }

    void mutex::set_spin_count(unsigned int count)
    {
        count;
    }

    semaphore::semaphore(long initialCount, long maximumCount, const char* pName)
    {
        maximumCount, pName;
        CRNLIB_ASSERT(maximumCount >= initialCount);

        auto sem = sem_open("semaphore", O_CREAT, 0777, initialCount);
        if (!sem)
        {
            CRNLIB_FAIL("semaphore: sem_init() failed");
        }
        m_sem = sem;
    }

    semaphore::~semaphore()
    {
        sem_close(m_sem);
    }

    void semaphore::release(long releaseCount)
    {
        CRNLIB_ASSERT(releaseCount >= 1);

        int status = 0;
#ifdef WIN32
        if (1 == releaseCount)
        {
            status = sem_post(m_sem);
        }
        else
        {
            status = sem_post_multiple(m_sem, releaseCount);
        }
#else
        while (releaseCount > 0)
        {
            status = sem_post(m_sem);
            if (status)
            {
                break;
            }
            releaseCount--;
        }
#endif

        if (status)
        {
            CRNLIB_FAIL("semaphore: sem_post() or sem_post_multiple() failed");
        }
    }

    void semaphore::try_release(long releaseCount)
    {
        CRNLIB_ASSERT(releaseCount >= 1);

#ifdef WIN32
        if (1 == releaseCount)
        {
            sem_post(m_sem);
        }
        else
        {
            sem_post_multiple(m_sem, releaseCount);
        }
#else
        while (releaseCount > 0)
        {
            sem_post(m_sem);
            releaseCount--;
        }
#endif
    }

    bool semaphore::wait(uint32 milliseconds)
    {
        int status;
        if (milliseconds == cUINT32_MAX)
        {
            status = sem_wait(m_sem);
        }
        else
        {
            struct timespec interval;
            interval.tv_sec = milliseconds / 1000;
            interval.tv_nsec = (milliseconds % 1000) * 1000000L;
            status = sem_timedwait(m_sem, &interval);
        }

        if (status)
        {
            if (errno != ETIMEDOUT)
            {
                CRNLIB_FAIL("semaphore: sem_wait() or sem_timedwait() failed");
            }
            return false;
        }

        return true;
    }

#if defined(CRN_OS_LINUX)
    spinlock::spinlock()
    {
        if (pthread_spin_init(&m_spinlock, 0))
        {
            CRNLIB_FAIL("spinlock: pthread_spin_init() failed");
        }
    }

    spinlock::~spinlock()
    {
        pthread_spin_destroy(&m_spinlock);
    }

    void spinlock::lock()
    {
        if (pthread_spin_lock(&m_spinlock))
        {
            CRNLIB_FAIL("spinlock: pthread_spin_lock() failed");
        }
    }

    void spinlock::unlock()
    {
        if (pthread_spin_unlock(&m_spinlock))
        {
            CRNLIB_FAIL("spinlock: pthread_spin_unlock() failed");
        }
    }
#elif defined(CRN_OS_DARWIN)
    spinlock::spinlock() :
        m_spinlock(OS_UNFAIR_LOCK_INIT)
    {
    }

    spinlock::~spinlock()
    {
    }

    void spinlock::lock()
    {
        os_unfair_lock_lock(&m_spinlock);
    }

    void spinlock::unlock()
    {
        os_unfair_lock_unlock(&m_spinlock);
    }
#else
    spinlock::spinlock()
    {
        __asm__ __volatile__("" ::: "memory");
        m_spinlock = 0;
    }

    spinlock::~spinlock()
    {
    }

    void spinlock::lock()
    {
        while (1)
        {
            int i;
            for (i = 0; i < 10000; i++)
            {
                if (__sync_bool_compare_and_swap(&m_spinlock, 0, 1))
                {
                    return 0;
                }
            }
            sched_yield();
        }
    }

    void spinlock::unlock()
    {
        __asm__ __volatile__("" ::: "memory");
        m_spinlock = 0;
    }
#endif

    task_pool::task_pool():
        m_num_threads(0),
        m_tasks_available(0, 32767),
        m_all_tasks_completed(0, 1),
        m_total_submitted_tasks(0),
        m_total_completed_tasks(0),
        m_exit_flag(false)
    {
        utils::zero_object(m_threads);
    }

    task_pool::task_pool(uint num_threads):
        m_num_threads(0),
        m_tasks_available(0, 32767),
        m_all_tasks_completed(0, 1),
        m_total_submitted_tasks(0),
        m_total_completed_tasks(0),
        m_exit_flag(false)
    {
        utils::zero_object(m_threads);

        bool status = init(num_threads);
        CRNLIB_VERIFY(status);
    }

    task_pool::~task_pool()
    {
        deinit();
    }

    bool task_pool::init(uint num_threads)
    {
        CRNLIB_ASSERT(num_threads <= cMaxThreads);
        num_threads = math::minimum<uint>(num_threads, cMaxThreads);

        deinit();

        bool succeeded = true;

        m_num_threads = 0;
        while (m_num_threads < num_threads)
        {
            int status = pthread_create(&m_threads[m_num_threads], nullptr, thread_func, this);
            if (status)
            {
                succeeded = false;
                break;
            }

            m_num_threads++;
        }

        if (!succeeded)
        {
            deinit();
            return false;
        }

        return true;
    }

    void task_pool::deinit()
    {
        if (m_num_threads)
        {
            join();

            atomic_exchange32(&m_exit_flag, true);

            m_tasks_available.release(m_num_threads);

            for (uint i = 0; i < m_num_threads; i++)
            {
                pthread_join(m_threads[i], nullptr);
            }

            m_num_threads = 0;

            atomic_exchange32(&m_exit_flag, false);
        }

        m_task_stack.clear();
        m_total_submitted_tasks = 0;
        m_total_completed_tasks = 0;
    }

    bool task_pool::queue_task(task_callback_func pFunc, uint64 data, void* pData_ptr)
    {
        CRNLIB_ASSERT(pFunc);

        task tsk;
        tsk.m_callback = pFunc;
        tsk.m_data = data;
        tsk.m_pData_ptr = pData_ptr;
        tsk.m_flags = 0;

        atomic_increment32(&m_total_submitted_tasks);
        if (!m_task_stack.try_push(tsk))
        {
            atomic_increment32(&m_total_completed_tasks);
            return false;
        }

        m_tasks_available.release(1);

        return true;
    }

    // It's the object's responsibility to delete pObj within the execute_task() method, if needed!
    bool task_pool::queue_task(executable_task* pObj, uint64 data, void* pData_ptr)
    {
        CRNLIB_ASSERT(pObj);

        task tsk;
        tsk.m_pObj = pObj;
        tsk.m_data = data;
        tsk.m_pData_ptr = pData_ptr;
        tsk.m_flags = cTaskFlagObject;

        atomic_increment32(&m_total_submitted_tasks);
        if (!m_task_stack.try_push(tsk))
        {
            atomic_increment32(&m_total_completed_tasks);
            return false;
        }

        m_tasks_available.release(1);

        return true;
    }

    void task_pool::process_task(task& tsk)
    {
        if (tsk.m_flags & cTaskFlagObject)
        {
            tsk.m_pObj->execute_task(tsk.m_data, tsk.m_pData_ptr);
        }
        else
        {
            tsk.m_callback(tsk.m_data, tsk.m_pData_ptr);
        }

        if (atomic_increment32(&m_total_completed_tasks) == m_total_submitted_tasks)
        {
            // Try to signal the semaphore (the max count is 1 so this may actually fail).
            m_all_tasks_completed.try_release();
        }
    }

    void task_pool::join()
    {
        // Try to steal any outstanding tasks. This could cause one or more worker threads to wake up and immediately go back to sleep, which is wasteful but should be harmless.
        task tsk;
        while (m_task_stack.pop(tsk))
        {
            process_task(tsk);
        }

        // At this point the task stack is empty.
        // Now wait for all concurrent tasks to complete. The m_all_tasks_completed semaphore has a max count of 1, so it's possible it could have saturated to 1 as the tasks
        // where issued and asynchronously completed, so this loop may iterate a few times.
        const int total_submitted_tasks = atomic_add32(&m_total_submitted_tasks, 0);
        while (m_total_completed_tasks != total_submitted_tasks)
        {
            // If the previous (m_total_completed_tasks != total_submitted_tasks) check failed the semaphore MUST be eventually signalled once the last task completes.
            // So I think this can actually be an INFINITE delay, but it shouldn't really matter if it's 1ms.
            m_all_tasks_completed.wait(1);
        }
    }

    void* task_pool::thread_func(void* pContext)
    {
        task_pool* pPool = static_cast<task_pool*>(pContext);
        task tsk;

        for (;;)
        {
            if (!pPool->m_tasks_available.wait())
            {
                break;
            }

            if (pPool->m_exit_flag)
            {
                break;
            }

            if (pPool->m_task_stack.pop(tsk))
            {
                pPool->process_task(tsk);
            }
        }

        return nullptr;
    }
} // namespace crnlib

#endif  // CRNLIB_USE_PTHREADS_API
