#ifndef PIPE_H
#define PIPE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>
#include <stddef.h>
//#include <stdlib.h>
//#include <string.h>

#ifndef TS_PIPE_DATA_TYPE
#		define TS_PIPE_DATA_TYPE unsigned int
#endif // TS_PIPE_DATA_TYPE

#ifndef TS_STATIC_ASSERT
#		if __STDC_VERSION__ == 201112L
/// _Static_assert of C11.
#				define TS_STATIC_ASSERT _Static_assert // static_assert can be used in both c/c++.
#		else
// Simple substitution for static assert.
/// Final implementation.
#				define TS_STATIC_ASSERT2_(cond, msg, name) static char __check__##name[cond ? 1 : -1]
/// Middle layer to unfold "__LINE__".
#				define TS_STATIC_ASSERT_(cond, msg, name)  TS_STATIC_ASSERT2_(cond, msg, name)
/// Entry of static assert.
#				define TS_STATIC_ASSERT(cond, msg)         TS_STATIC_ASSERT_(cond, msg, __LINE__)
#		endif
#endif // TS_STATIC_ASSERT

#include "./pipe_atomic.h"

enum
{
		TS_PIPE_SIZE_LOG2 = 8,
		TS_PIPE_SIZE = 2 << TS_PIPE_SIZE_LOG2,
		TS_PIPE_MASK = TS_PIPE_SIZE - 1,
		TS_PIPE_READABLE = 0x11111111,
		TS_PIPE_WRITABLE = 0x00000000,
		TS_PIPE_INVALID = 0xFFFFFFFF
};

TS_STATIC_ASSERT(TS_PIPE_SIZE_LOG2 < 32, "");

typedef TS_PIPE_DATA_TYPE TSpipedata;

struct TSpipe
{
		/// Data of the pipe.
		TSpipedata buffer[TS_PIPE_SIZE];

		// Volatile means "easy to change" and can be consiedered as "direct access to raw
		// memory addresses". "Volatile" is caused by external factors, such as
		// multithreading, interruptions, etc.

		/// Can be "TS_PIPE_INVALID", "TS_PIPE_READABLE" and "TS_PIPE_WRITABLE"
		uint32_t volatile flags[TS_PIPE_SIZE];

		// Not like std::atomic in c++11, usually we need to align data in (double) word
		// to make it atomic.
		// Notice that increment/decrement operations(like a++, --a) and compound assignment
		// operations(like a=1, a+=2, c<<=3) are read-modify-write atomic operations with
		// total sequentially consistent ordering (as if using TS_SEQ_CST).
		// __attribute__ directive:
		// https://gcc.gnu.org/onlinedocs/gcc-3.2/gcc/Variable-Attributes.html C11 _Atomic:
		// https://en.cppreference.com/w/c/language/atomic.

		/// Changed in "tsPipeWriterTryWriteFront" and "tsPipeWriterTryReadFront".
		uint32_t volatile writeIndex __attribute__((aligned(4)));

		/// Changed only in "tsPipeWriterTryReadFront".
		uint32_t volatile readIndex __attribute__((aligned(4)));

		/// Counts of total already read buffers. Written only in "tsPipeReaderTryReadBack" to
		/// indicate a chunk of buffer has been successfull read.
		uint32_t volatile readCount __attribute__((aligned(4)));
};

typedef struct TSpipe TSpipe;

/// Initialize the pipe. Except "buffer" field, clear the other bytes of the pipe.
static inline void
tsPipeInit(TSpipe *pipe)
{
		memset(pipe->buffer, 0, sizeof(pipe->buffer));
		pipe->readIndex = 0;
		pipe->writeIndex = 0;
		pipe->readCount = 0;
}

/// Not intended for general use. Should only be used very prudently.
static inline int
tsPipeIsEmpty(TSpipe *pipe)
{
		return tsAtomicLoad_u32(&pipe->writeIndex, TS_RELAXED) -
		           tsAtomicLoad_u32(&pipe->readCount, TS_RELAXED) ==
		       0;
}

/// Return 0 if we were unable to read.
/// Thread safe for both multiple readers and the writer.
static int
tsPipeReaderTryReadBack(TSpipe *pipe, TSpipedata *out)
{
		uint32_t actualReadIndex;
		uint32_t readCount = tsAtomicLoad_u32(&pipe->readCount, TS_RELAXED);

		// We get hold of read index for consistency and do first pass starting at read count.
		uint32_t readIndexToUse = readCount;
		while (1)
		{
				uint32_t writeIndex = tsAtomicLoad_u32(&pipe->writeIndex, TS_RELAXED);
				uint32_t numInPipe = writeIndex - readCount;
				if (0 == numInPipe) { return 0; }

				if (readIndexToUse >= writeIndex)
				{
						readIndexToUse = tsAtomicLoad_u32(&pipe->readIndex, TS_RELAXED);
				}

				actualReadIndex = readIndexToUse & TS_PIPE_MASK;

				// Multiple potential readers mean we should check if the data is valid,
				// using an atomic compare exchange.
				uint32_t expected = TS_PIPE_READABLE;
				uint32_t desired = TS_PIPE_INVALID;
				TSbool success = tsAtomicCmpXchg_u32(
				    &pipe->flags[actualReadIndex], &expected, &desired, 1, TS_ACQ_REL, TS_RELAXED);
				if (success) break;

				// Proceed to previous data (towards pipe->writeIndex, which is the head).
				++readIndexToUse;

				// Update read count.
				readCount = tsAtomicLoad_u32(&pipe->readCount, TS_RELAXED);
		}

		// We update the read index using an atomic add, as we've only read one piece of data.
		// this ensure consistency of the read index, and the above loop ensures readers
		// only read from unread data.
		tsAtomicFetchAdd_u32(&pipe->readCount, 1, TS_RELAXED);

		// Now read data, ensuring we do so after above reads & CAS.
		*out = pipe->buffer[actualReadIndex];

		tsAtomicStore_u32(&pipe->flags[actualReadIndex], TS_PIPE_WRITABLE, TS_RELEASE);

		return 1;
}

/// "tsPipeWriterTryReadFront" returns 0 if we were unable to read.
/// This is thread safe for the single writer, but should not be called by readers.
static int
tsPipeWriterTryReadFront(TSpipe *pipe, TSpipedata *out)
{
		uint32_t writeIndex = tsAtomicLoad_u32(&pipe->writeIndex, TS_RELAXED);
		uint32_t frontReadIndex = writeIndex;

		// Multiple potential readers mean we should check if the data is valid,
		// using an atomic compare exchange - which acts as a form of lock (so not quite
		// lockless really).
		uint32_t actualReadIndex = 0;
		while (1)
		{
				uint32_t readCount = tsAtomicLoad_u32(&pipe->readCount, TS_RELAXED);
				uint32_t numInPipe = writeIndex - readCount;
				if (0 == numInPipe)
				{
						tsAtomicStore_u32(&pipe->readIndex, readCount, TS_RELEASE);
						return 0;
				}
				--frontReadIndex;
				actualReadIndex = frontReadIndex & TS_PIPE_MASK;
				uint32_t expected = TS_PIPE_READABLE;
				uint32_t desired = TS_PIPE_INVALID;
				TSbool success = tsAtomicCmpXchg_u32(
				    &pipe->flags[actualReadIndex], &expected, &desired, 1, TS_ACQ_REL, TS_RELAXED);
				if (success) { break; }
				else if (tsAtomicLoad_u32(&pipe->readIndex, TS_ACQUIRE) >= frontReadIndex)
				{
						return 0;
				}
		}

		// Now read data, ensuring we do so after above reads & CAS
		*out = pipe->buffer[actualReadIndex];

		tsAtomicStore_u32(&pipe->flags[actualReadIndex], TS_PIPE_WRITABLE, TS_RELAXED);
		tsAtomicStore_u32(&pipe->writeIndex, writeIndex - 1, TS_RELAXED);

		return 1;
}

/// WriterTryWriteFront returns false if we were unable to write
/// This is thread safe for the single writer, but should not be called by readers
static int
tsPipeWriterTryWriteFront(TSpipe *pipe, TSpipedata *in)
{
		// The writer 'owns' the write index, and readers can only reduce
		// the amount of data in the pipe.
		// We get hold of both values for consistency and to reduce 0 sharing
		// impacting more than one access
		uint32_t writeIndex = pipe->writeIndex;

		// power of two sizes ensures we can perform AND for a modulus
		uint32_t actualWriteIndex = writeIndex & TS_PIPE_MASK;

		// a reader may still be reading this item, as there are multiple readers
		if (tsAtomicLoad_u32(&pipe->flags[actualWriteIndex], TS_ACQUIRE) != TS_PIPE_WRITABLE)
		{
				return 0; // still being read, so have caught up with tail.
		}

		// as we are the only writer we can update the data without atomics
		//  whilst the write index has not been updated
		pipe->buffer[actualWriteIndex] = *in;
		tsAtomicStore_u32(&pipe->flags[actualWriteIndex], TS_PIPE_READABLE, TS_RELEASE);

		tsAtomicFetchAdd_u32(&pipe->writeIndex, 1, TS_RELAXED);
		return 1;
}

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif // PIPE_H

#ifdef PIPE_TEST
#define CONSUMER_COUNT 4
#define MAX_IDS        65535

TSpipe pipe = {0};

static unsigned int currentId = 0;
static unsigned int volatile allIds[MAX_IDS] __attribute__((aligned(4)));

#if (defined _WIN32 && (defined _M_IX86 || defined _M_X64)) || \
    (defined __i386__ || defined __x86_64__)
static inline void
tsSpinWait(uint32_t spinCount)
{
		uint64_t end = __rdtsc() + spinCount;
		while (__rdtsc() < end) { _mm_pause(); }
}
#else
static inline void
tsSpinWait(uint32_t spinCount)
{
		while (spinCount)
		{
				// TODO: may have NOP or yield equiv.
				--spinCount;
		}
}
#endif

void *
producer(void *arg)
{
		while (1)
		{
				if (currentId == MAX_IDS) return NULL;
				TSpipedata toWrite = currentId;
				if (tsPipeWriterTryWriteFront(&pipe, &toWrite))
				{
						currentId++;
						tsSpinWait(25000000); // 4,294,967,295
				}
				else { _mm_pause(); }
		}
		return NULL;
}

void *
consumer(void *arg)
{
		uintptr_t threadId = (uintptr_t)arg;
		while (1)
		{
				TSpipedata out;
				if (tsPipeReaderTryReadBack(&pipe, &out))
				{
						// Read data successfully.
						__atomic_fetch_add(&allIds[out], 1, __ATOMIC_RELAXED);
						tsSpinWait(100000000);
				}
				else { _mm_pause(); }
		}
		return NULL;
}

int
main(void)
{
		pthread_t producerThread, consumerThreads[CONSUMER_COUNT];

		memset((void *)allIds, 0, sizeof(allIds));

		// Init pipe.
		tsPipeInit(&pipe);

		// Create threads.
		int status;
		status = pthread_create(&producerThread, NULL, producer, NULL);
		assert(status == 0);
		for (int i = 0; i < CONSUMER_COUNT; i++)
		{
				void *arg = (void *)(uintptr_t)i;
				status = pthread_create(&consumerThreads[i], NULL, consumer, arg);
				assert(status == 0);
		}

		pthread_join(producerThread, NULL);
		for (int i = 0; i < CONSUMER_COUNT; i++) pthread_kill(consumerThreads[i], 114);

		// Read unread data.
		for (;;)
		{
				TSpipedata out;
				if (tsPipeReaderTryReadBack(&pipe, &out))
				{
						allIds[out] = -1; // Mark unread when threads were killed.
				}
				else { break; }
		}

		// Validate.
		for (int i = 0; i < MAX_IDS; i++)
		{
				if (allIds[i] != 1) printf("%d:%d\n", i, allIds[i]);
		}

		return 0;
}
#endif // PIPE_TEST