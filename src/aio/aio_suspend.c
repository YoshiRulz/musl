#include <aio.h>
#include <errno.h>
#include <time.h>
#include "atomic.h"
#include "libc.h"
#include "pthread_impl.h"

extern volatile int __aio_fut;

int aio_suspend(const struct aiocb *const cbs[], int cnt, const struct timespec *ts)
{
	int i, tid = 0, ret, expect = 0;
	struct timespec at;
	volatile int dummy_fut, *pfut;
	int nzcnt = 0;
	const struct aiocb *cb = 0;

	if (cnt<0) {
		errno = EINVAL;
		return -1;
	}

	for (i=0; i<cnt; i++) if (cbs[i]) {
		if (aio_error(cbs[i]) != EINPROGRESS) return 0;
		nzcnt++;
		cb = cbs[i];
	}

	if (ts) {
		clock_gettime(CLOCK_MONOTONIC, &at);
		at.tv_sec += ts->tv_sec;
		if ((at.tv_nsec += ts->tv_nsec) >= 1000000000) {
			at.tv_nsec -= 1000000000;
			at.tv_sec++;
		}
	}

	for (;;) {
		for (i=0; i<cnt; i++)
			if (cbs[i] && aio_error(cbs[i]) != EINPROGRESS)
				return 0;

		switch (nzcnt) {
		case 0:
			pfut = &dummy_fut;
			break;
		case 1:
			pfut = (void *)&cb->__err;
			expect = EINPROGRESS | 0x80000000;
			a_cas(pfut, EINPROGRESS, expect);
			break;
		default:
			pfut = &__aio_fut;
			if (!tid) tid = __pthread_self()->tid;
			expect = a_cas(pfut, 0, tid);
			if (!expect) expect = tid;
			/* Need to recheck the predicate before waiting. */
			for (i=0; i<cnt; i++)
				if (cbs[i] && aio_error(cbs[i]) != EINPROGRESS)
					return 0;
			break;
		}

		ret = __timedwait(pfut, expect, CLOCK_MONOTONIC, ts?&at:0, 0, 0, 1);

		if (ret) {
			errno = ret==ETIMEDOUT ? EAGAIN : ret;
			return -1;
		}
	}
}

LFS64(aio_suspend);
