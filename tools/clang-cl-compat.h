/* Force-included into every file Mesa compiles with clang-cl (build.ps1).
 *
 * Mesa's src/util/u_atomic.h takes MSVC's intrinsics whenever _MSC_VER is
 * defined, which clang-cl defines too, and calls some by lowercase names
 * (_interlockedexchange64 and others) that only MSVC's own intrin.h
 * declares. They are the uppercase intrinsics clang provides, or an add
 * that returns the new value. Nothing else in Mesa uses those names.
 */
#pragma once
#if defined(__clang__) && defined(_MSC_VER)
#include <intrin.h>

static __inline long murrtty_interlockedadd(long volatile *target, long value)
{
   return _InterlockedExchangeAdd(target, value) + value;
}

static __inline __int64 murrtty_interlockedadd64(__int64 volatile *target, __int64 value)
{
   return _InterlockedExchangeAdd64(target, value) + value;
}

#define _interlockedadd(target, value) murrtty_interlockedadd((target), (value))
#define _interlockedadd64(target, value) murrtty_interlockedadd64((target), (value))
#define _interlockedincrement64(target) _InterlockedIncrement64(target)
#define _interlockeddecrement64(target) _InterlockedDecrement64(target)
#define _interlockedexchange64(target, value) _InterlockedExchange64((target), (value))
#define _interlockedexchangeadd64(target, value) _InterlockedExchangeAdd64((target), (value))
#endif
