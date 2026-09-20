// SPDX-License-Identifier: GPL-2.0-only
/*
 *  RISC-V BRS SBI requirement tests
 *
 * Copyright (C) 2026, Institute of Software, CAS., Xiang W <wangxiang@iscas.ac.cn>
 */

#include <libcflat.h>
#include <devicetree.h>
#include <asm/sbi.h>
#include <asm/isa.h>
#include <asm/smp.h>

#ifdef CONFIG_EFI
#include <acpi.h>
#endif

static void check_sbi_010(void)
{
	struct sbiret ret;
	unsigned long major, minor;

	report_prefix_push("SBI_010");

	ret = sbi_get_spec_version();
	if (ret.error)
		report_fail("sbi_get_spec_version failed with %ld", ret.error);
	else {
		major = ret.value >> SBI_SPEC_VERSION_MAJOR_SHIFT;
		major = major & SBI_SPEC_VERSION_MAJOR_MASK;
		minor = ret.value & SBI_SPEC_VERSION_MINOR_MASK;

		report(ret.value >= sbi_mk_version(2, 0),
			"SBI spec version is v2.0 or later (current %lu.%lu)", major, minor);
	}

	report_prefix_pop();
}

static void check_sbi_020(void)
{
	report_prefix_push("SBI_020");
	report(sbi_probe(SBI_EXT_HSM), "HSM extension is implemented");
	report_prefix_pop();
}

static void check_sbi_030(void)
{
	report_prefix_push("SBI_030");

	if (cpu_has_extension(smp_processor_id(), ISA_SSTC))
		report_skip("Sstc is present, TIME is not mandated");
	else
		report(sbi_probe(SBI_EXT_TIME), "TIME extension is implemented");

	report_prefix_pop();
}

#define IMSIC_SLEVEL_IRQ	9
static bool hart_has_imsic[NR_CPUS];

#ifdef CONFIG_EFI
static int acpi_collect_imsic(struct acpi_subtable_header *header)
{
	struct acpi_madt_rintc *rintc = (void *)header;
	int cpu;

	if (!rintc->imsic_size)
		return 0;

	cpu = hartid_to_cpu(rintc->hart_id);
	if (cpu >= 0 && cpu < nr_cpus)
		hart_has_imsic[cpu] = true;

	return 0;
}
#endif

static int dt_intc_phandle_to_cpu(const void *fdt, uint32_t phandle)
{
	int intc_node, cpu_node, cpus_node, ret, i;
	struct dt_reg raw_reg;
	u32 nac, nsc;
	u64 hartid;

	intc_node = fdt_node_offset_by_phandle(fdt, phandle);
	if (intc_node < 0)
		return -1;

	/* cpu-intc -> cpu node -> /cpus */
	cpu_node = fdt_parent_offset(fdt, intc_node);
	if (cpu_node < 0)
		return -1;

	cpus_node = fdt_parent_offset(fdt, cpu_node);
	if (cpus_node < 0)
		return -1;

	/* #address-cells / #size-cells live on /cpus, not on the cpu node */
	ret = dt_get_nr_cells(cpus_node, &nac, &nsc);
	if (ret < 0)
		return -1;

	dt_reg_init(&raw_reg, nac, nsc);
	ret = dt_get_reg(cpu_node, 0, &raw_reg);
	if (ret < 0)
		return -1;

	hartid = raw_reg.address_cells[0];
	if (nac == 2)
		hartid = (hartid << 32) | raw_reg.address_cells[1];

	for (i = 0; i < nr_cpus; i++) {
		if (cpus[i].hartid == hartid)
			return i;
	}
	return -1;
}

static void dt_collect_imsic(void)
{
	const void *fdt = dt_fdt();
	const fdt32_t *prop;
	int node, len, ncells, i, cpu;
	uint32_t phandle, irq;

	node = -1;
	while ((node = fdt_node_offset_by_compatible(fdt, node, "riscv,imsics")) >= 0) {
		prop = fdt_getprop(fdt, node, "interrupts-extended", &len);
		if (!prop || len < 0)
			continue;

		/* Each entry is <phandle irq>  (cpu-intc #interrupt-cells = 1) */
		ncells = len / (int)sizeof(fdt32_t);
		for (i = 0; i + 1 < ncells; i += 2) {
			phandle = fdt32_to_cpu(prop[i]);
			irq = fdt32_to_cpu(prop[i + 1]);

			if (irq != IMSIC_SLEVEL_IRQ)
				continue;

			cpu = dt_intc_phandle_to_cpu(fdt, phandle);
			if (cpu >= 0 && cpu < nr_cpus)
				hart_has_imsic[cpu] = true;
		}
	}
}

static bool all_harts_have_imsic(void)
{
	int cpu;

	memset(hart_has_imsic, 0, sizeof(hart_has_imsic));

	if (dt_available())
		dt_collect_imsic();
#ifdef CONFIG_EFI
	else
		acpi_table_parse_madt(ACPI_MADT_TYPE_RINTC, acpi_collect_imsic);
#endif

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		if (!cpu_present(cpu))
			continue;
		if (!cpu_has_extension(cpu, ISA_SSAIA)) {
			report_info("CPU %d (hartid %lu) has no Ssaia",
				    cpu, cpus[cpu].hartid);
			return false;
		}
		if (!hart_has_imsic[cpu]) {
			report_info("CPU %d (hartid %lu) has no S-level IMSIC",
				    cpu, cpus[cpu].hartid);
			return false;
		}
	}

	return true;
}

static void check_sbi_040(void)
{
	report_prefix_push("SBI_040");

	if (all_harts_have_imsic())
		report_skip("IMSIC is present, IPI is not mandated");
	else
		report(sbi_probe(SBI_EXT_IPI), "IPI extension is implemented");

	report_prefix_pop();
}

static void check_sbi_050(void)
{
	report_prefix_push("SBI_050");

	if (all_harts_have_imsic())
		report_skip("IMSIC is present, RFENCE is not mandated");
	else
		report(sbi_probe(SBI_EXT_RFNC), "RFENCE extension is implemented");

	report_prefix_pop();
}

static void check_sbi_060(void)
{
	int cpu = smp_processor_id();

	report_prefix_push("SBI_060");

	if (cpu_has_extension(cpu, ISA_SSCSRIND) && cpu_has_extension(cpu, ISA_SSCCFG))
		/*
		 * TODO: OE_SBI_060_010 also requires Smcsrind and Smcdeleg,
		 * but they are M-mode extensions and not present in the S-mode
		 * ISA string (DT or RHCT). Revisit if BRS clarifies how to detect them.
		 */
		report_skip("Sscsrind and Ssccfg are present, PMU is not mandated");
	else
		report(sbi_probe(SBI_EXT_PMU), "PMU extension is implemented");

	report_prefix_pop();
}

static void check_sbi_070(void)
{
	report_prefix_push("SBI_070");

	if (dt_available())
		report_skip("ACPI not available, DBCN is not mandated");
	else {
#ifdef CONFIG_EFI
		struct spcr_descriptor *spcr;

		spcr = find_acpi_table_addr(SPCR_SIGNATURE);
		if (spcr == NULL)
			report_skip("SPCR is not present, DBCN is not mandated");
		else {
			if (spcr->interface_type == 0x15)
				report(sbi_probe(SBI_EXT_DBCN), "DBCN extension is implemented");
			else
				report_skip(
					"SPCR interface type is %#x (not 0x15), DBCN is not mandated",
					spcr->interface_type);
		}
#else
		report_skip("ACPI not available, DBCN is not mandated");
#endif
	}

	report_prefix_pop();
}

int main(int argc, char **argv)
{
	report_prefix_push("riscv-brs");

	check_sbi_010();
	check_sbi_020();
	check_sbi_030();
	check_sbi_040();
	check_sbi_050();
	check_sbi_060();
	check_sbi_070();

	return report_summary();
}
