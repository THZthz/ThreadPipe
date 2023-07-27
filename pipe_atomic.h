
/* █████╗ ████████╗ ██████╗ ███╗   ███╗██╗ ██████╗*/
/*██╔══██╗╚══██╔══╝██╔═══██╗████╗ ████║██║██╔════╝*/
/*███████║   ██║   ██║   ██║██╔████╔██║██║██║     */
/*██╔══██║   ██║   ██║   ██║██║╚██╔╝██║██║██║     */
/*██║  ██║   ██║   ╚██████╔╝██║ ╚═╝ ██║██║╚██████╗*/
/*╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚═╝     ╚═╝╚═╝ ╚═════╝*/

// Atomic type is a data type with no data racing. If one thread is writing an atomic
// type and another thread reading it, this kind of action is well-defined. In addition,
// access to atomic objects can establish inter-thread synchronization and sort nonatomic
// memory accesses in the order specified by "enum TSmemorder".

// Below are types and functions which wrap gcc built-in atomic extensions.
// This can be re-written to make the program available in other compilers and platforms.

// GCC __atomic_*: https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html.

/// Memory orders, map to the C++11 memory orders with the same names.
enum TSmemorder
{
		// GCC extensions note ---------------------------------------------------------------
		// The memory order parameter in GCC extensions is a signed int,
		// but only the lower 16 bits are reserved for the memory order.
		// The remainder of the signed int is reserved for target use and should be 0.
		// Use of the predefined atomic values ensures proper usage.

		/// Do not guarantee any execution order, only make sure it is atomic operation.
		TS_RELAXED = __ATOMIC_RELAXED,
		/// In current thread, all **subsequent read** operations should be happened **after**
		/// current atomic operation. (**I read first, you read next**)
		TS_ACQUIRE = __ATOMIC_ACQUIRE,
		/// In current thread, all **preceding write** operations should be happened
		/// **before** current atomic operation. (**You write first, I write next**)
		TS_RELEASE = __ATOMIC_RELEASE,
		/// Include Acuqire and Release at the same time.
		TS_ACQ_REL = __ATOMIC_ACQ_REL,
		/// In current thread, all subsequent operations related to current atomic data
		/// should be placed after current atomic operation.
		TS_CONSUME = __ATOMIC_CONSUME,
		/// Make sure atomic operations are executed in the order of the code.
		TS_SEQ_CST = __ATOMIC_SEQ_CST,
};

static inline uint32_t __attribute__((always_inline))
tsAtomicLoad_u32(const uint32_t volatile *dst, enum TSmemorder order)
{
		return __atomic_load_n(dst, order);
}

static inline void __attribute__((always_inline))
tsAtomicStore_u32(uint32_t volatile *dst, uint32_t val, enum TSmemorder order)
{
		__atomic_store_n(dst, val, order);
}

static inline int __attribute__((always_inline)) tsAtomicCmpXchg_u32(
    uint32_t volatile *ptr,
    const uint32_t *expected,
    const uint32_t *desired,
    int weak,
    enum TSmemorder successOrder,
    enum TSmemorder failureOrder)
{
		return __atomic_compare_exchange(
		    ptr, (uint32_t *)expected, (uint32_t *)desired, weak, successOrder, failureOrder);
}

static inline uint32_t __attribute__((always_inline))
tsAtomicFetchAdd_u32(uint32_t volatile *ptr, uint32_t val, enum TSmemorder memorder)
{
		return __atomic_fetch_add(ptr, val, memorder);
}
