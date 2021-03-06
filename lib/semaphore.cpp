/*

Copyright (c) 2017, NVIDIA Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#ifdef __semaphore_cuda
namespace cuda
{
#else
#include "semaphore"
namespace std
{
#endif //__semaphore_cuda

namespace experimental
{
inline namespace v1 {
namespace details
{
#ifdef __semaphore_cuda

#else

#ifdef __linux__
// On Linux, we make use of the kernel memory wait operations. These have been available for a long time.
template <class A>
inline const void *__semaphore_fixalign(A &a)
{
    static_assert(sizeof(A) <= sizeof(int), "Linux only supports 'int' for Futex.");
    return (const void *)((intptr_t)&a & ~(sizeof(int) - 1));
}
inline int __semaphore_readint(const void *p)
{
    int i;
    memcpy(&i, p, sizeof(int));
    return i;
}
template <class A, class V>
inline void __semaphore_wait(A &a, V v)
{
    auto p = __semaphore_fixalign(a);
    auto i = __semaphore_readint(p);
    asm volatile("" ::
                     : "memory");
    if (a.load(memory_order_relaxed) != v)
        return;
    syscall(SYS_futex, p, FUTEX_WAIT_PRIVATE, i, 0, 0, 0);
}
template <class A, class V, class Rep, class Period>
void __semaphore_wait_timed(A &a, V v, const chrono::duration<Rep, Period> &t)
{
    auto p = __semaphore_fixalign(a);
    auto i = __semaphore_readint(p);
    asm volatile("" ::
                     : "memory");
    if (a.load(memory_order_relaxed) != v)
        return;
    syscall(SYS_futex, p, FUTEX_WAIT_PRIVATE, i, __semaphore_to_timespec(t), 0, 0);
}
template <class A>
inline void __semaphore_wake_one(A &a)
{
    syscall(SYS_futex, __semaphore_fixalign(a), FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
}
template <class A>
inline void __semaphore_wake_all(A &a)
{
    syscall(SYS_futex, __semaphore_fixalign(a), FUTEX_WAKE_PRIVATE, INT_MAX, 0, 0, 0);
}
template <class A, class V>
inline void __semaphore_wait(volatile A &a, V v)
{
    auto p = __semaphore_fixalign(a);
    auto i = __semaphore_readint(p);
    asm volatile("" ::
                     : "memory");
    if (a.load(memory_order_relaxed) != v)
        return;
    syscall(SYS_futex, p, FUTEX_WAIT, i, 0, 0, 0);
}
template <class A, class V, class Rep, class Period>
void __semaphore_wait_timed(volatile A &a, V v, const chrono::duration<Rep, Period> &t)
{
    auto p = __semaphore_fixalign(a);
    auto i = __semaphore_readint(p);
    asm volatile("" ::
                     : "memory");
    if (a.load(memory_order_relaxed) != v)
        return;
    syscall(SYS_futex, p, FUTEX_WAIT, i, details::__semaphore_to_timespec(t), 0, 0);
}
template <class A>
inline void __semaphore_wake_one(volatile A &a)
{
    syscall(SYS_futex, __semaphore_fixalign(a), FUTEX_WAKE, 1, 0, 0, 0);
}
template <class A>
inline void __semaphore_wake_all(volatile A &a)
{
    syscall(SYS_futex, __semaphore_fixalign(a), FUTEX_WAKE, INT_MAX, 0, 0, 0);
}
#endif // __linux__

#if defined(WIN32) && _WIN32_WINNT >= 0x0602
// On Windows, we make use of the kernel memory wait operations as well. These first became available with Windows 8.
template <class A, class V>
void __semaphore_wait(A &a, V v)
{
    static_assert(sizeof(V) <= 8, "Windows only allows sizes between 1B and 8B for WaitOnAddress.");
    WaitOnAddress((PVOID)&a, (PVOID)&v, sizeof(v), -1);
}
template <class A, class V, class Rep, class Period>
void __semaphore_wait_timed(A &a, V v, chrono::duration<Rep, Period> const &delta)
{
    static_assert(sizeof(V) <= 8, "Windows only allows sizes between 1B and 8B for WaitOnAddress.");
    WaitOnAddress((PVOID)&a, (PVOID)&v, sizeof(v), (DWORD)chrono::duration_cast<chrono::milliseconds>(delta).count());
}
template <class A>
inline void __semaphore_wake_one(A &a)
{
    WakeByAddressSingle((PVOID)&a);
}
template <class A>
inline void __semaphore_wake_all(A &a)
{
    WakeByAddressAll((PVOID)&a);
}
#endif // defined(WIN32) && _WIN32_WINNT >= 0x0602

#endif // __semaphore_cuda
}

#ifdef __semaphore_fast_path
__semaphore_abi void binary_semaphore::__release_slow(count_type old, std::memory_order order, semaphore_notify notify) noexcept
{
    count_type lock = 0;
    do {
        old &= ~__lockbit;
        lock = (old & __slowbit) ? __lockbit : 0;
    } while (!atom.compare_exchange_weak(old, (old | lock) & ~(__valubit | __slowbit), order, std::memory_order_relaxed));
    if (lock != 0)
    {
        switch (notify)
        {
        case semaphore_notify_all:
            details::__semaphore_wake_all(atom);
            break;
        case semaphore_notify_one:
            details::__semaphore_wake_one(atom);
            break;
        case semaphore_notify_none:
            break;
        }
        atom.fetch_and(~__lockbit, std::memory_order_release);
    }
}
#endif

__semaphore_abi void binary_semaphore::__acquire_slow(std::memory_order order) noexcept
{
    auto const maxdiff = (std::numeric_limits<count_type>::max)() >> 1;

    details::__semaphore_exponential_backoff b;
    auto old = atom.fetch_add(__contbit, std::memory_order_acquire);
    auto const tick = ticket.fetch_add(1, std::memory_order_relaxed);
    auto tock = tocket.load(std::memory_order_relaxed);
    auto ready = (tock >= tick || tick - tock > maxdiff);
    for (int i = 0; ; ++i) {
        if(i < 64)
            details::__semaphore_yield();
        else 
        {
#ifdef __semaphore_fast_path
            old = atom.fetch_or(__slowbit, std::memory_order_acquire) | __slowbit;
            if (!ready || (old & __valubit) != 0)
                details::__semaphore_wait(atom, old);
#else
            b.sleep();
#endif
        }
        if(!ready) {
            tock = tocket.load(std::memory_order_relaxed);
            ready = (tock >= tick || tick - tock > maxdiff);
            if(ready)
                b.reset();
            else
                continue;
        }
        old = atom.load(std::memory_order_relaxed);
        while ((old & __valubit) == 0) {
            old &= ~__lockbit;
            auto next = old - __contbit + __valubit;
            if (atom.compare_exchange_weak(old, next, order, std::memory_order_relaxed))
                return;
        }
    }
}

bool binary_semaphore::__acquire_slow_timed(std::chrono::time_point<details::__semaphore_clock, details::__semaphore_duration> const &abs_time, std::memory_order order) noexcept
{

    count_type old = atom.load(order);
    count_type const expectbit = 0; //(set ? __valubit : 0);
    if ((old & __valubit) != expectbit)
    {
        details::__semaphore_exponential_backoff b;
#ifdef __semaphore_fast_path
        for (int i = 0; i < 2; ++i)
        {
#else
        while (1)
        {
#endif
            if (details::__semaphore_clock::now() > abs_time)
                return false;
            b.sleep();
            old = atom.load(order);
            if ((old & __valubit) == expectbit)
                break;
        }
    }
#ifdef __semaphore_fast_path
    if ((old & __valubit) != expectbit)
    {
        while (1)
        {
            old = atom.fetch_or(__contbit, std::memory_order_relaxed) | __contbit;
            if ((old & __valubit) == expectbit)
                break;
            auto const delay = abs_time - details::__semaphore_clock::now();
            if (delay < details::__semaphore_duration(0))
                return false;
            details::__semaphore_wait_timed(atom, old, delay);
            old = atom.load(order);
            if ((old & __valubit) == expectbit)
                break;
        }
    }
    while (old & __lockbit)
        old = atom.load(std::memory_order_relaxed);
#endif
    return true;
}

#ifndef __semaphore_sem

__semaphore_abi bool counting_semaphore::__fetch_sub_if_slow(counting_semaphore::count_type old, std::memory_order order) noexcept
{

    do
    {
        old &= ~__lockmask;
        if (atom.compare_exchange_weak(old, old - (1 << __shift), order, std::memory_order_relaxed))
            return true;
    } while ((old >> __shift) >= 1);

    return false;
}

#ifdef __semaphore_fast_path
void counting_semaphore::__fetch_add_slow(counting_semaphore::count_type term, counting_semaphore::count_type old, std::memory_order order, semaphore_notify notify) noexcept
{
    while (1)
    {

        bool const apply_lock = ((old & __contmask) != 0) && (notify != semaphore_notify::none);
        int const set = ((old & __valumask) + (term << __shift)) | (apply_lock ? __lockmask : 0);

        old &= ~__lockmask;
        if (atom.compare_exchange_weak(old, set, order, std::memory_order_relaxed))
        {
            if (apply_lock)
            {
                switch (notify)
                {
                case semaphore_notify_all:
                    details::__semaphore_wake_all(atom);
                    break;
                case semaphore_notify_one:
                    details::__semaphore_wake_one(atom);
                    break;
                case semaphore_notify_none:
                    break;
                }
                atom.fetch_and(~__lockmask, std::memory_order_relaxed);
            }
            break;
        }
    }
}
#endif

__semaphore_abi void counting_semaphore::__acquire_slow(std::memory_order order) noexcept
{

    int old;
    details::__semaphore_exponential_backoff b;
#ifdef __semaphore_fast_path
    for (int i = 0; i < 2; ++i)
    {
#else
    while (1)
    {
#endif
        b.sleep();
        old = atom.load(order);
        if ((old >> __shift) >= 1)
            goto done;
    }
#ifdef __semaphore_fast_path
    while (1)
    {
        old = atom.fetch_or(__contmask, std::memory_order_relaxed) | __contmask;
        if ((old >> __shift) >= 1)
            goto done;
        details::__semaphore_wait(atom, old);
        old = atom.load(order);
        if ((old >> __shift) >= 1)
            goto done;
    }
#endif
done:
#ifdef __semaphore_fast_path
    while (old & __lockmask)
        old = atom.load(std::memory_order_relaxed);
#else
    ;
#endif
}
#endif

#ifndef __semaphore_cuda

static constexpr int __atomic_wait_table_entry_size = sizeof(synchronic) > alignof(synchronic) ? sizeof(synchronic) : alignof(synchronic);

static constexpr int __atomic_wait_table_entry_count = 1024;

__semaphore_managed alignas(__atomic_wait_table_entry_size) unsigned char __atomic_wait_table[__atomic_wait_table_entry_count][__atomic_wait_table_entry_size] = { 0 };

__semaphore_abi size_t __atomic_wait_table_index(void const* ptr) {

    return ((uintptr_t)ptr / __atomic_wait_table_entry_size) & (__atomic_wait_table_entry_count - 1);
}

__semaphore_abi synchronic *__atomic_wait_get_semaphore(void const *a)
{
    return (synchronic *)&__atomic_wait_table[__atomic_wait_table_index(a)][0];
}

#endif
}
} // namespace experimental
} // namespace std
