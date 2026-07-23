/*
 * METAL-004 — session affinity selects a fixed CPU instead of RR.
 */
#include <stdio.h>

static unsigned m_rr;
static unsigned m_affinity_cpu;
static int      m_affinity_on;

static unsigned
pick_cpu(unsigned n_cpus)
{
	unsigned ticket;

	if (m_affinity_on) {
		return m_affinity_cpu;
	}
	ticket = m_rr++;
	return ticket % n_cpus;
}

int
main(void)
{
	unsigned a, b, c;

	m_rr = 0;
	m_affinity_on = 0;
	a = pick_cpu(4);
	b = pick_cpu(4);
	c = pick_cpu(4);
	if (a != 0 || b != 1 || c != 2) {
		fprintf(stderr, "metal004: RR broken (%u %u %u)\n", a, b, c);
		return 1;
	}

	m_affinity_cpu = 3;
	m_affinity_on  = 1;
	a = pick_cpu(4);
	b = pick_cpu(4);
	if (a != 3 || b != 3) {
		fprintf(stderr, "metal004: affinity broken (%u %u)\n", a, b);
		return 1;
	}

	m_affinity_on = 0;
	a = pick_cpu(4);
	if (a != 3) { /* RR continues from prior counter */
		fprintf(stderr, "metal004: RR resume broken (%u)\n", a);
		return 1;
	}

	printf("metal004: ok\n");
	return 0;
}
