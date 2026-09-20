// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, Ventana Micro Systems Inc., Andrew Jones <ajones@ventanamicro.com>
 */
#include <libcflat.h>
#include <bitops.h>
#include <devicetree.h>
#include <string.h>
#include <asm/isa.h>
#include <asm/setup.h>

typedef void (*isa_func_t)(const char *, int, void *);

struct isa_info {
	unsigned long id;
	isa_func_t func;
	void *data;
};

static bool isa_match(const char *ext, const char *name, int len)
{
	return len == strlen(ext) && !strncasecmp(name, ext, len);
}

struct isa_check {
	const char *ext;
	bool found;
};

static void isa_name(const char *name, int len, void *data)
{
	struct isa_check *check = (struct isa_check *)data;

	if (isa_match(check->ext, name, len))
		check->found = true;
}

static void isa_bit(const char *name, int len, void *data)
{
	struct thread_info *info = (struct thread_info *)data;

#define ISA_MATCH_AND_SET(ext) \
	if (isa_match(#ext, name, len)) \
		set_bit(ISA_##ext, info->isa)

	ISA_MATCH_AND_SET(SMCDELEG);
	ISA_MATCH_AND_SET(SMCSRIND);
	ISA_MATCH_AND_SET(SSAIA);
	ISA_MATCH_AND_SET(SSCCFG);
	ISA_MATCH_AND_SET(SSCSRIND);
	ISA_MATCH_AND_SET(SSTC);

#undef ISA_MATCH_AND_SET
}

static void isa_parse(const char *isa_string, int len, struct isa_info *info)
{
	assert(isa_string[0] == 'r' && isa_string[1] == 'v');
#if __riscv_xlen == 32
	assert(isa_string[2] == '3' && isa_string[3] == '2');
#else
	assert(isa_string[2] == '6' && isa_string[3] == '4');
#endif

	for (int i = 4; i < len; ++i) {
		if (isa_string[i] == '_') {
			const char *multi = &isa_string[++i];
			int start = i;

			while (i < len - 1 && isa_string[i] != '_')
				++i;
			info->func(multi, i - start, info->data);
			if (i < len - 1)
				--i;
		} else {
			info->func(&isa_string[i], 1, info->data);
		}
	}
}

static void isa_parse_fdt(int cpu_node, u64 hartid, void *data)
{
	struct isa_info *info = (struct isa_info *)data;
	const struct fdt_property *prop;
	int len;

	if (hartid != info->id)
		return;

	prop = fdt_get_property(dt_fdt(), cpu_node, "riscv,isa", &len);
	assert(prop);

	isa_parse(prop->data, len, info);
}

#ifdef CONFIG_EFI
#include <acpi.h>

static void isa_parse_acpi(struct acpi_table_rhct *rhct, struct acpi_rhct_node_hart_info *hart_info, void *data)
{
	struct acpi_rhct_node *node;
	struct acpi_rhct_node_isa_string *isa_string;
	struct isa_info *info = (struct isa_info *)data;
	if (hart_info->uid != info->id)
		return;
	for (int i = 0; i < hart_info->num_offsets; i++) {
		node = (void *)rhct + hart_info->nodes[i];
		if (node->type == RHCT_NODE_ISA_STRING) {
			isa_string = (void *) node;
			isa_parse(isa_string->isa, isa_string->isa_length, info);
		}
	}
}

static void acpi_rhct_for_each_hart(
	void (*func)(struct acpi_table_rhct *, struct acpi_rhct_node_hart_info *, void *), void *data)
{
	struct acpi_table_rhct *rhct;
	struct acpi_rhct_node *node;
	struct acpi_rhct_node_hart_info *hart_info;
	rhct = find_acpi_table_addr(RHCT_SIGNATURE);
	assert_msg(rhct, "Cannot find ACPI RHCT");
	node = (void *)rhct + rhct->node_offset;
	for (int i = 0; i < rhct->node_count; i++) {
		if (node->type == RHCT_NODE_HART_INFO) {
			hart_info = (void *)node;
			func(rhct, hart_info, data);
		}
		assert(node->length >= sizeof(*node));
		node = (void *)node + node->length;
	}
}
#endif

void isa_init(struct thread_info *ti)
{
	struct isa_info info = {
		.func = isa_bit,
		.data = ti,
	};
	int ret;

	if (dt_available()) {
		info.id = ti->hartid;
		ret = dt_for_each_cpu_node(isa_parse_fdt, &info);
		assert(ret == 0);
	} else {
#ifdef CONFIG_EFI
		info.id = ti->uid;
		acpi_rhct_for_each_hart(isa_parse_acpi, &info);
#else
		assert_msg(false, "ACPI not available");
#endif
	}
}

bool cpu_has_extension_name(int cpu, const char *ext)
{
	struct isa_info info = {
		.func = isa_name,
		.data = &(struct isa_check){ .ext = ext, },
	};
	struct isa_check *check = info.data;
	int ret;

	if (dt_available()) {
		info.id = cpus[cpu].hartid;
		ret = dt_for_each_cpu_node(isa_parse_fdt, &info);
		assert(ret == 0);
	} else {
#ifdef CONFIG_EFI
		info.id = cpus[cpu].uid;
		acpi_rhct_for_each_hart(isa_parse_acpi, &info);
#else
		assert_msg(false, "ACPI not available");
#endif
	}

	return check->found;
}
