#define pr_fmt(fmt) "%s:%s():%d: " fmt, KBUILD_MODNAME, __func__, __LINE__
#include "linux/moduleparam.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/cpu.h>
#include "tlm.h"

#define CREATE_TRACE_POINTS
DEFINE_TRACE(modtimerlat_latency);
DEFINE_TRACE(modtimerlat_latency_exceeded);

#define IRQ_CONTEXT 0
#define PERIOD_US 1000LL

struct timerlat {
    	struct task_struct *kthread;
    	ktime_t abs_period;
    	struct hrtimer timer;
};
/*
 * Hold the max latency so far
 */
static atomic64_t max_latency = ATOMIC64_INIT(0);

static bool stop_on_exceeded = false;
module_param(stop_on_exceeded, bool, 0660);
MODULE_PARM_DESC(stop_on_exceeded, "When the latency exceeds the threshold, stop sampling.");

static long latency_threshold_us = 0;
module_param(latency_threshold_us, long, 0660);
MODULE_PARM_DESC(latency_threshold_us, "The threshold to generate a latency exceeded trace, in usecs.");

DEFINE_PER_CPU(struct task_struct *, kthread);

static void update_max_latency(s64 latency)
{
	s64 cur_max_latency = atomic64_read(&max_latency);

	while (latency > cur_max_latency)
		cur_max_latency = atomic64_cmpxchg(&max_latency, cur_max_latency, latency);
}

static enum hrtimer_restart tlm_irq(struct hrtimer *timer)
{
    	s64 now, latency;
    	struct timerlat *tlat = container_of(timer, struct timerlat, timer);

    	now = ktime_to_ns(hrtimer_cb_get_time(timer));
    	latency = now - ktime_to_ns(tlat->abs_period);

    	if (unlikely(latency_threshold_us && latency > latency_threshold_us * 1000LL))
        	trace_modtimerlat_latency_exceeded(latency);
    	else
        	trace_modtimerlat_latency(latency);

        update_max_latency(latency);

    	wake_up_process(tlat->kthread);

    	return HRTIMER_NORESTART;
}

static void wait_next_period(struct timerlat *tlat)
{
    	s64 now, next_period;

    	now = ktime_to_ns(hrtimer_cb_get_time(&tlat->timer));
    	next_period = now + PERIOD_US * 1000LL;
    	tlat->abs_period = ns_to_ktime(next_period);

    	set_current_state(TASK_INTERRUPTIBLE);
    	hrtimer_start(&tlat->timer, tlat->abs_period, HRTIMER_MODE_ABS_PINNED);
    	schedule();
}

static int tlm_loop(void *data)
{
    	struct timerlat tlat;

    	hrtimer_init(&tlat.timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
    	tlat.timer.function = tlm_irq;
    	tlat.kthread = current;

    	while (!kthread_should_stop())
        	wait_next_period(&tlat);

    	hrtimer_cancel(&tlat.timer);

    	return 0;
}

static void stop_kthreads(void)
{
	unsigned int cpu;
	struct task_struct **kt;

	for_each_online_cpu(cpu) {
		kt = per_cpu_ptr(&kthread, cpu);
		if (*kt) {
			kthread_stop(*kt);
			*kt = NULL;
		}
	}
}

static struct task_struct *create_thread_on_cpu(unsigned int cpu)
{
	struct task_struct *p;
	char comm[TASK_COMM_LEN];
	struct cpumask mask;

	snprintf(comm, TASK_COMM_LEN, "tlm/%u", cpu);

	p = kthread_create_on_node(tlm_loop, NULL, cpu_to_node(cpu), comm);
	if (unlikely(IS_ERR(p)))
		return p;

	cpumask_clear(&mask);
	cpumask_set_cpu(cpu, &mask);
	set_cpus_allowed_ptr(p, &mask);

	return p;
}

static int start_kthread(unsigned int cpu)
{
	struct task_struct *kthr;

	kthr = create_thread_on_cpu(cpu);

	if (unlikely(IS_ERR(kthr))) {
		pr_err("Could not start sampling thread on cpu %u\n", cpu);
		stop_kthreads();
	} else {
		*per_cpu_ptr(&kthread, cpu) = kthr;
	}

	return PTR_ERR_OR_ZERO(kthr);
}

static int start_kthreads(void)
{
	unsigned int cpu;
	int ret;

	cpus_read_lock();

	for_each_possible_cpu(cpu)
		*per_cpu_ptr(&kthread, cpu) = NULL;

	for_each_online_cpu(cpu) {
		ret = start_kthread(cpu);
		if (ret)
			break;
	}

	cpus_read_unlock();

	return ret;
}

static int __init tlm_init(void)
{
	return start_kthreads();
}

static void __exit tlm_exit(void)
{
	stop_kthreads();
    	pr_warn("Max latency: %lld\n", atomic64_read(&max_latency));
}

module_init(tlm_init);
module_exit(tlm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wander Lairson Costa");
MODULE_DESCRIPTION("A timerlat tracer like module");
