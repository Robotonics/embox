/**
 * @file lthread_test.c
 * @brief simple test for lthreads
 *
 * @author Andrey Kokorev
 * @date    16.02.2014
 */
#include <err.h>
#include <embox/test.h>
#include <kernel/sched.h>
#include <kernel/lthread/lthread.h>
#include <kernel/lthread/lthread_priority.h>
#include <kernel/time/ktime.h>

#define LTHREAD_QUANTITY OPTION_GET(NUMBER, lthreads_quantity)

EMBOX_TEST_SUITE("test for lthread API");

static volatile int done = 0;

static void *run1(void *arg) {
	test_emit('a');
	done++;
	return NULL;
}

TEST_CASE("Launch simple lthread") {
	struct lthread *lt;

	lt = lthread_create(run1, NULL);
	test_assert_zero(err(lt));
	lthread_launch(lt);

	/* Spin, waiting lthread finished */
	while(1) {
		if(done == 1) break;
		ksleep(0);
	}

	test_assert_emitted("a");
}

static void *run_resched(void *arg) {
	ksleep(0);
	done = 1;
	return NULL;
}

TEST_CASE("Call sched from lthread") {
	struct lthread *lt;

	done = 0;

	lt = lthread_create(run_resched, NULL);
	test_assert_zero(err(lt));
	lthread_launch(lt);

	/* Spin, waiting lthread finished */
	while(1) {
		if(done == 1) break;
		ksleep(0);
	}
}

static void *run2(void *arg) {
	done++;
	return NULL;
}

TEST_CASE("Create lthreads with different priorities") {
	done = 0;

	for(int i = 0; i < LTHREAD_QUANTITY; i++) {
		struct lthread *lt;

		lt = lthread_create(run2, NULL);
		test_assert_zero(err(lt));
		test_assert_zero(
			runnable_priority_set(&lt->runnable, LTHREAD_PRIORITY_MIN + i)
		);
		lthread_launch(lt);
	}

	/* Spin, waiting all lthreads finished */
	while(1) {
		if(done == LTHREAD_QUANTITY) break;
		ksleep(0);
	}
}

static void *run3(void *arg) {
	test_emit('b');
	done++;
	return NULL;
}

TEST_CASE("Test executing order") {
	struct lthread *lt1, *lt2;

	done = 0;

	lt1 = lthread_create(run1, NULL);
	test_assert_zero(err(lt1));
	runnable_priority_set(&lt1->runnable, LTHREAD_PRIORITY_MIN);

	lt2 = lthread_create(run3, NULL);
	test_assert_zero(err(lt2));
	runnable_priority_set(&lt2->runnable, LTHREAD_PRIORITY_MAX);

	/* prevent scheduling to avoid executing one
	 * before adding another to runq
	 */
	sched_lock();
	{
		lthread_launch(lt1);
		lthread_launch(lt2);
	}
	sched_unlock();

	while(done < 2) {
		ksleep(0);
	}
	test_assert_emitted("ba");
}
