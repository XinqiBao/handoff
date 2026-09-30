# Linux Measurement Host

This document owns host preparation for controlled Linux timing. The
[benchmark methodology](benchmark-methodology.md) defines workload meaning and
claim strength; [reproducibility](reproducibility.md) defines revision, build,
result, and sidecar procedures. Host isolation reduces interference but cannot
promise an interruption-free CPU: firmware SMIs, some managed interrupts, and
shared cache, memory, and package power remain possible influences. Qualify the
host against the intended claim before collecting comparative evidence.

## Choose the CPU layout

Discover physical cores, SMT siblings, NUMA nodes, shared caches, online CPUs,
and the process's allowed CPU mask. Assign one physical core to each timed worker
unless sharing is the question. Reserve at least one other core for the operating
system, device interrupts, unpinned work, and the benchmark coordinator. A route
with more simultaneous workers than isolated cores needs a different host or an
explicitly different placement experiment. Do not silently count SMT siblings as
independent cores; keep the sibling of a worker CPU idle or offline when present.

Record the selected worker and housekeeping CPU lists, their topology, the route's
role-to-CPU mapping, and the reason for that mapping. Keep compared routes on the
same placement. The benchmark's Linux affinity readback verifies each requested
worker mask; affinity alone does not prevent other work from using that CPU.

## Isolate the worker CPUs

There are two distinct ways to keep ordinary tasks off worker CPUs:

- A cgroup v2 `cpuset.cpus.partition=isolated` partition is runtime-configurable
  and reversible. Every timed worker must join that partition before requesting
  its affinity, while the coordinator remains on housekeeping CPUs. The current
  benchmark pins worker threads but does not move individual threads into
  cgroups. Putting its entire process in a worker-only partition would also put
  the coordinator there; this route needs explicit harness support before use.
- The boot-time `isolcpus=domain,managed_irq,<workers>` option removes worker
  CPUs from ordinary load balancing and tries to steer managed IRQs away. An
  explicitly pinned worker can still run there, so this works with the current
  harness. Domain isolation cannot be undone until another reboot. The
  `managed_irq` part is best effort: some device queues cannot be moved.

For lower timer and RCU interference, add `nohz_full=<workers>` at boot. A kernel
with `CONFIG_NO_HZ_FULL=y` is required. Linux automatically offloads RCU callbacks
and excludes these CPUs from the lockup watchdog by default; an additional
`rcu_nocbs=` list is normally redundant. The full tick stops only when its
conditions hold, including a single runnable task on the CPU. Mechanism-side
`yield` calls, syscalls, unavoidable local interrupts, and firmware SMIs can
still interrupt execution.

Set `irqaffinity=<housekeeping>` in the boot configuration for default device
IRQ placement. Bootloader syntax and commands vary by distribution: retain the
previous kernel command line and a working rollback path, regenerate the boot
configuration, reboot, and read back `/proc/cmdline`,
`/sys/devices/system/cpu/isolated`, and
`/sys/devices/system/cpu/nohz_full`. The CPU lists are host-specific; no list in
this repository is a default for another machine.

After boot, restrict `/sys/devices/virtual/workqueue/cpumask` to the housekeeping
CPUs using its CPU-mask syntax. Read it back. Inspect `/proc/interrupts` and
`/proc/irq/*/effective_affinity_list` under representative device activity;
move any remaining movable device IRQs away from workers. An active irqbalance
daemon must be configured consistently or it may rewrite masks. Restoring an
IRQ's allowed mask need not restore its previous *effective* CPU, so verify both
when undoing a temporary change. Keep unneeded devices quiet where practical.

Boot-time domain isolation also leaves ordinary build and maintenance work on
housekeeping CPUs. Giving a build process a broad affinity mask that includes
isolated CPUs does not restore load balancing onto them. Keep builds outside
measurement sessions; accept slower builds on housekeeping CPUs or use a
separate non-isolated boot profile for frequent development. Recheck the live
isolation and frequency state after switching profiles.

## Control frequency, power, and temperature

Inventory the CPU frequency driver, governor, HWP or equivalent hardware
management, base and turbo ranges, power limits, cooling policy, and thermal
throttle counters. Select a frequency that all active roles can sustain after
temperature reaches a stable range, with margin below power and thermal limits.
Do not assume the maximum advertised frequency is an all-core setting. On CPUs
with a low non-turbo base frequency, disabling turbo can leave only that low
range available.

Where the platform exposes per-policy `scaling_min_freq`, `scaling_max_freq`, and
`scaling_governor`, a session may set both frequency limits to the qualified
target and select a performance-focused governor. First save every policy's
minimum, maximum, governor, and energy preference; expand an existing policy
range if necessary before setting the target. Read back each policy and restore
the saved values at session end. For a target already within the current policy
range, the Linux sysfs operation is:

```sh
: "${TARGET_KHZ:?set the qualified target frequency in kHz}"
for policy in /sys/devices/system/cpu/cpufreq/policy*; do
  printf '%s\n' "$TARGET_KHZ" | sudo tee "$policy/scaling_max_freq" >/dev/null
  printf '%s\n' "$TARGET_KHZ" | sudo tee "$policy/scaling_min_freq" >/dev/null
  printf 'performance\n' | sudo tee "$policy/scaling_governor" >/dev/null
done
```

Apply it only after capturing the old values, and restore them in an order that
keeps each minimum no greater than its maximum. These controls are normally
writable at runtime and must be checked again after a reboot. They are requests
to the hardware, not proof of the frequency delivered under load. Check actual
per-core busy frequency using
APERF/MPERF-derived measurements such as `turbostat`, and check power,
temperature, and throttle counters during sustained representative work. Keep
thermal protection enabled. Change firmware power or cooling settings only when
the machine exposes them, they are needed, and their effects can be verified.
BIOS changes are not a prerequisite when boot isolation and verified runtime
frequency control meet the host qualification criteria.

The target frequency, CPU lists, IRQ numbers, power limits, and BIOS controls
must be selected on the measurement host. Record them beside each result group;
do not embed one host's values in benchmark code or a shared run command.

## Qualify and run

Before relying on a host configuration, verify all of the following under a
sustained representative workload long enough to reach its thermal operating
range. Qualification applies to the tested placement and workload shape; a
different role count, pacing, or power demand needs its own relevant check:

1. Worker and housekeeping CPUs have the intended topology, isolation state,
   and exact effective thread affinities.
2. No unrelated runnable task or avoidable device IRQ lands on a worker CPU;
   inspect scheduler and interrupt activity rather than inferring this from
   `taskset` alone. Investigate any managed IRQ or firmware SMI that remains.
3. Actual busy frequency stays within the declared tolerance on every worker;
   package power and temperature remain stable, with no new throttle events.
4. The chosen monitoring tools and output activity do not add unequal work to
   compared routes. Use diagnostic runs when instrumentation would disturb the
   timed measurement.

Failure of a relevant condition means the host is not qualified for the
proposed precision claim. Fix the source or narrow the claim; repetition and
warmup cannot substitute for isolation or frequency control. Once qualified,
follow the exact-revision, correctness, workload, and raw-result procedures in
the linked documents. Requalify after a kernel, firmware, topology, cooling,
power-policy, or significant workload change.

## Reboot handoff

Before changing boot settings, preserve the old and intended command lines,
bootloader update command, rollback method, remote-access dependence, selected
CPU layout, saved runtime controls, source revision, and pending post-boot checks
in a bounded local note. Mark each step as planned, applied, or verified. After
reboot, inspect the live machine rather than trusting the note's planned state.
Keep host-specific notes and raw calibration in ignored local storage; commit
only durable, host-independent procedure and selected evidence needed to support
a project conclusion. Do not record credentials or session transcripts.

The Linux [kernel parameter](https://docs.kernel.org/admin-guide/kernel-parameters.html),
[cgroup v2](https://docs.kernel.org/admin-guide/cgroup-v2.html), and
[Intel P-state](https://docs.kernel.org/admin-guide/pm/intel_pstate.html)
documents define the platform mechanisms. Firedancer's
[initialization guide](https://docs.firedancer.io/guide/initializing.html) is an
inspiration for separating IRQ, workqueue, cpuset, and boot-time controls; this
project does not use its configuration tool or claim validator compatibility.
