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

	report_prefix_push("SBI_010");

	ret = sbi_get_spec_version();
	if (ret.error) {
		report_fail("sbi_get_spec_version failed with %ld", ret.error);
		report_prefix_pop();
		return;
	}

	report_info("SBI spec version %lu.%lu",
		    (ret.value >> SBI_SPEC_VERSION_MAJOR_SHIFT) &
		    SBI_SPEC_VERSION_MAJOR_MASK,
		    ret.value & SBI_SPEC_VERSION_MINOR_MASK);

	report(ret.value >= sbi_mk_version(2, 0),
	       "SBI spec version is v2.0 or later");

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

	if (cpu_has_extension(smp_processor_id(), ISA_SSTC)) {
		report_skip("Sstc is present, TIME is not mandated");
		report_prefix_pop();
		return;
	}

	report(sbi_probe(SBI_EXT_TIME), "TIME extension is implemented");
	report_prefix_pop();
}

static void check_sbi_040(void)
{
	report_prefix_push("SBI_040");

	if (cpu_has_extension(smp_processor_id(), ISA_SSAIA)) {
		report_skip("Ssaia is present, IPI is not mandated");
		report_prefix_pop();
		return;
	}

	report(sbi_probe(SBI_EXT_IPI), "IPI extension is implemented");
	report_prefix_pop();
}

static void check_sbi_050(void)
{
	report_prefix_push("SBI_050");

	if (cpu_has_extension(smp_processor_id(), ISA_SSAIA)) {
		report_skip("Ssaia is present, RFENCE is not mandated");
		report_prefix_pop();
		return;
	}

	report(sbi_probe(SBI_EXT_RFNC), "RFENCE extension is implemented");
	report_prefix_pop();
}

static void check_sbi_060(void)
{
	int cpu = smp_processor_id();
	bool skip = true;
	bool has_ext;

	report_prefix_push("SBI_060");

	has_ext = cpu_has_extension(cpu, ISA_SMCSRIND);
	if (!has_ext)
		report_info("Smcsrind is not present");
	skip &= has_ext;

	has_ext = cpu_has_extension(cpu, ISA_SSCSRIND);
	if (!has_ext)
		report_info("Sscsrind is not present");
	skip &= has_ext;

	has_ext = cpu_has_extension(cpu, ISA_SMCDELEG);
	if (!has_ext)
		report_info("Smcdeleg is not present");
	skip &= has_ext;

	has_ext = cpu_has_extension(cpu, ISA_SSCCFG);
	if (!has_ext)
		report_info("Ssccfg is not present");
	skip &= has_ext;

	if (skip) {
		report_skip("Smcsrind, Sscsrind, Smcdeleg and Ssccfg are present, PMU is not mandated");
		report_prefix_pop();
		return;
	}

	report(sbi_probe(SBI_EXT_PMU), "PMU extension is implemented");
	report_prefix_pop();
}

static void check_sbi_070(void)
{
	report_prefix_push("SBI_070");

#ifdef CONFIG_EFI
	if (!dt_available()) {
		struct spcr_descriptor *spcr;

		spcr = find_acpi_table_addr(SPCR_SIGNATURE);
		if (spcr) {
			report_info("SPCR interface type is %#x", spcr->interface_type);
			if (spcr->interface_type == 0x15) {
				report(sbi_probe(SBI_EXT_DBCN), "DBCN extension is implemented");
				report_prefix_pop();
				return;
			}
			report_skip("SPCR interface type is not 0x15, DBCN is not mandated");
			report_prefix_pop();
			return;
		}
		report_skip("SPCR is not present, DBCN is not mandated");
		report_prefix_pop();
		return;
	}
#endif
	report_skip("ACPI not available, DBCN is not mandated");
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
