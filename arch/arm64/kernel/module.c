// SPDX-License-Identifier: GPL-2.0-only
/*
 * AArch64 loadable module support.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#define pr_fmt(fmt) "Modules: " fmt

#include <linux/bitops.h>
#include <linux/elf.h>
#include <linux/ftrace.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleloader.h>
#include <linux/random.h>
#include <linux/scs.h>

#include <asm/alternative.h>
#include <asm/insn.h>
#include <asm/scs.h>
#include <asm/sections.h>
#include <asm/text-patching.h>

enum aarch64_reloc_op {
	RELOC_OP_NONE,
	RELOC_OP_ABS,
	RELOC_OP_PREL,
	RELOC_OP_PAGE,
};

static u64 do_reloc(enum aarch64_reloc_op reloc_op, __le32 *place, u64 val)
{
	switch (reloc_op) {
	case RELOC_OP_ABS:
		return val;
	case RELOC_OP_PREL:
		return val - (u64)place;
	case RELOC_OP_PAGE:
		return (val & ~0xfff) - ((u64)place & ~0xfff);
	case RELOC_OP_NONE:
		return 0;
	}

	pr_err("do_reloc: unknown relocation operation %d\n", reloc_op);
	return 0;
}

#define WRITE_PLACE(place, val, mod) do {				\
	__typeof__(val) __val = (val);					\
									\
	if (mod->state == MODULE_STATE_UNFORMED)			\
		*(place) = __val;					\
	else								\
		aarch64_insn_copy(place, &(__val), sizeof(*place));	\
} while (0)

static int reloc_data(enum aarch64_reloc_op op, void *place, u64 val, int len,
		      struct module *me)
{
	s64 sval = do_reloc(op, place, val);

	/*
	 * The ELF psABI for AArch64 documents the 16-bit and 32-bit place
	 * relative and absolute relocations as having a range of [-2^15, 2^16)
	 * or [-2^31, 2^32), respectively. However, in order to be able to
	 * detect overflows reliably, we have to choose whether we interpret
	 * such quantities as signed or as unsigned, and stick with it.
	 * The way we organize our address space requires a signed
	 * interpretation of 32-bit relative references, so let's use that
	 * for all R_AARCH64_PRELxx relocations. This means our upper
	 * bound for overflow detection should be Sxx_MAX rather than Uxx_MAX.
	 */

	switch (len) {
	case 16:
		WRITE_PLACE((s16 *)place, sval, me);
		switch (op) {
		case RELOC_OP_ABS:
			if (sval < 0 || sval > U16_MAX)
				return -ERANGE;
			break;
		case RELOC_OP_PREL:
			if (sval < S16_MIN || sval > S16_MAX)
				return -ERANGE;
			break;
		default:
			pr_err("Invalid 16-bit data relocation (%d)\n", op);
			return 0;
		}
		break;
	case 32:
		WRITE_PLACE((s32 *)place, sval, me);
		switch (op) {
		case RELOC_OP_ABS:
			if (sval < 0 || sval > U32_MAX)
				return -ERANGE;
			break;
		case RELOC_OP_PREL:
			if (sval < S32_MIN || sval > S32_MAX)
				return -ERANGE;
			break;
		default:
			pr_err("Invalid 32-bit data relocation (%d)\n", op);
			return 0;
		}
		break;
	case 64:
		WRITE_PLACE((s64 *)place, sval, me);
		break;
	default:
		pr_err("Invalid length (%d) for data relocation\n", len);
		return 0;
	}
	return 0;
}

enum aarch64_insn_movw_imm_type {
	AARCH64_INSN_IMM_MOVNZ,
	AARCH64_INSN_IMM_MOVKZ,
};

static int reloc_insn_movw(enum aarch64_reloc_op op, __le32 *place, u64 val,
			   int lsb, enum aarch64_insn_movw_imm_type imm_type,
			   struct module *me)
{
	u64 imm;
	s64 sval;
	u32 insn = le32_to_cpu(*place);

	sval = do_reloc(op, place, val);
	imm = sval >> lsb;

	if (imm_type == AARCH64_INSN_IMM_MOVNZ) {
		/*
		 * For signed MOVW relocations, we have to manipulate the
		 * instruction encoding depending on whether or not the
		 * immediate is less than zero.
		 */
		insn &= ~(3 << 29);
		if (sval >= 0) {
			/* >=0: Set the instruction to MOVZ (opcode 10b). */
			insn |= 2 << 29;
		} else {
			/*
			 * <0: Set the instruction to MOVN (opcode 00b).
			 *     Since we've masked the opcode already, we
			 *     don't need to do anything other than
			 *     inverting the new immediate field.
			 */
			imm = ~imm;
		}
	}

	/* Update the instruction with the new encoding. */
	insn = aarch64_insn_encode_immediate(AARCH64_INSN_IMM_16, insn, imm);
	WRITE_PLACE(place, cpu_to_le32(insn), me);

	if (imm > U16_MAX)
		return -ERANGE;

	return 0;
}

static int reloc_insn_imm(enum aarch64_reloc_op op, __le32 *place, u64 val,
			  int lsb, int len, enum aarch64_insn_imm_type imm_type,
			  struct module *me)
{
	u64 imm, imm_mask;
	s64 sval;
	u32 insn = le32_to_cpu(*place);

	/* Calculate the relocation value. */
	sval = do_reloc(op, place, val);
	sval >>= lsb;

	/* Extract the value bits and shift them to bit 0. */
	imm_mask = (BIT(lsb + len) - 1) >> lsb;
	imm = sval & imm_mask;

	/* Update the instruction's immediate field. */
	insn = aarch64_insn_encode_immediate(imm_type, insn, imm);
	WRITE_PLACE(place, cpu_to_le32(insn), me);

	/*
	 * Extract the upper value bits (including the sign bit) and
	 * shift them to bit 0.
	 */
	sval = (s64)(sval & ~(imm_mask >> 1)) >> (len - 1);

	/*
	 * Overflow has occurred if the upper bits are not all equal to
	 * the sign bit of the value.
	 */
	if ((u64)(sval + 1) >= 2)
		return -ERANGE;

	return 0;
}

static int reloc_insn_adrp(struct module *mod, Elf64_Shdr *sechdrs,
			   __le32 *place, u64 val, struct module *me)
{
	u32 insn;

	if (!is_forbidden_offset_for_adrp(place))
		return reloc_insn_imm(RELOC_OP_PAGE, place, val, 12, 21,
				      AARCH64_INSN_IMM_ADR, me);

	/* patch ADRP to ADR if it is in range */
	if (!reloc_insn_imm(RELOC_OP_PREL, place, val & ~0xfff, 0, 21,
			    AARCH64_INSN_IMM_ADR, me)) {
		insn = le32_to_cpu(*place);
		insn &= ~BIT(31);
	} else {
		/* out of range for ADR -> emit a veneer */
		val = module_emit_veneer_for_adrp(mod, sechdrs, place, val & ~0xfff);
		if (!val)
			return -ENOEXEC;
		insn = aarch64_insn_gen_branch_imm((u64)place, val,
						   AARCH64_INSN_BRANCH_NOLINK);
	}

	WRITE_PLACE(place, cpu_to_le32(insn), me);
	return 0;
}

int apply_relocate_add(Elf64_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	unsigned int i;
	int ovf;
	bool overflow_check;
	Elf64_Sym *sym;
	void *loc;
	u64 val;
	Elf64_Rela *rel = (void *)sechdrs[relsec].sh_addr;

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* loc corresponds to P in the AArch64 ELF document. */
		loc = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;

		/* sym is the ELF symbol we're referring to. */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rel[i].r_info);

		/* val corresponds to (S + A) in the AArch64 ELF document. */
		val = sym->st_value + rel[i].r_addend;

		/* Check for overflow by default. */
		overflow_check = true;

		/* Perform the static relocation. */
		switch (ELF64_R_TYPE(rel[i].r_info)) {
		/* Null relocations. */
		case R_ARM_NONE:
		case R_AARCH64_NONE:
			ovf = 0;
			break;

		/* Data relocations. */
		case R_AARCH64_ABS64:
			overflow_check = false;
			ovf = reloc_data(RELOC_OP_ABS, loc, val, 64, me);
			break;
		case R_AARCH64_ABS32:
			ovf = reloc_data(RELOC_OP_ABS, loc, val, 32, me);
			break;
		case R_AARCH64_ABS16:
			ovf = reloc_data(RELOC_OP_ABS, loc, val, 16, me);
			break;
		case R_AARCH64_PREL64:
			overflow_check = false;
			ovf = reloc_data(RELOC_OP_PREL, loc, val, 64, me);
			break;
		case R_AARCH64_PREL32:
			ovf = reloc_data(RELOC_OP_PREL, loc, val, 32, me);
			break;
		case R_AARCH64_PREL16:
			ovf = reloc_data(RELOC_OP_PREL, loc, val, 16, me);
			break;

		/* MOVW instruction relocations. */
		case R_AARCH64_MOVW_UABS_G0_NC:
			overflow_check = false;
			fallthrough;
		case R_AARCH64_MOVW_UABS_G0:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 0,
					      AARCH64_INSN_IMM_MOVKZ, me);
			break;
		case R_AARCH64_MOVW_UABS_G1_NC:
			overflow_check = false;
			fallthrough;
		case R_AARCH64_MOVW_UABS_G1:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 16,
					      AARCH64_INSN_IMM_MOVKZ, me);
			break;
		case R_AARCH64_MOVW_UABS_G2_NC:
			overflow_check = false;
			fallthrough;
		case R_AARCH64_MOVW_UABS_G2:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 32,
					      AARCH64_INSN_IMM_MOVKZ, me);
			break;
		case R_AARCH64_MOVW_UABS_G3:
			/* We're using the top bits so we can't overflow. */
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 48,
					      AARCH64_INSN_IMM_MOVKZ, me);
			break;
		case R_AARCH64_MOVW_SABS_G0:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 0,
					      AARCH64_INSN_IMM_MOVNZ, me);
			break;
		case R_AARCH64_MOVW_SABS_G1:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 16,
					      AARCH64_INSN_IMM_MOVNZ, me);
			break;
		case R_AARCH64_MOVW_SABS_G2:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 32,
					      AARCH64_INSN_IMM_MOVNZ, me);
			break;
		case R_AARCH64_MOVW_PREL_G0_NC:
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 0,
					      AARCH64_INSN_IMM_MOVKZ, me);
			break;
		case R_AARCH64_MOVW_PREL_G0:
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 0,
					      AARCH64_INSN_IMM_MOVNZ, me);
			break;
		case R_AARCH64_MOVW_PREL_G1_NC:
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 16,
					      AARCH64_INSN_IMM_MOVKZ, me);
			break;
		case R_AARCH64_MOVW_PREL_G1:
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 16,
					      AARCH64_INSN_IMM_MOVNZ, me);
			break;
		case R_AARCH64_MOVW_PREL_G2_NC:
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 32,
					      AARCH64_INSN_IMM_MOVKZ, me);
			break;
		case R_AARCH64_MOVW_PREL_G2:
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 32,
					      AARCH64_INSN_IMM_MOVNZ, me);
			break;
		case R_AARCH64_MOVW_PREL_G3:
			/* We're using the top bits so we can't overflow. */
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 48,
					      AARCH64_INSN_IMM_MOVNZ, me);
			break;

		/* Immediate instruction relocations. */
		case R_AARCH64_LD_PREL_LO19:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2, 19,
					     AARCH64_INSN_IMM_19, me);
			break;
		case R_AARCH64_ADR_PREL_LO21:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 0, 21,
					     AARCH64_INSN_IMM_ADR, me);
			break;
		case R_AARCH64_ADR_PREL_PG_HI21_NC:
			overflow_check = false;
			fallthrough;
		case R_AARCH64_ADR_PREL_PG_HI21:
			ovf = reloc_insn_adrp(me, sechdrs, loc, val, me);
			if (ovf && ovf != -ERANGE)
				return ovf;
			break;
		case R_AARCH64_ADD_ABS_LO12_NC:
		case R_AARCH64_LDST8_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 0, 12,
					     AARCH64_INSN_IMM_12, me);
			break;
		case R_AARCH64_LDST16_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 1, 11,
					     AARCH64_INSN_IMM_12, me);
			break;
		case R_AARCH64_LDST32_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 2, 10,
					     AARCH64_INSN_IMM_12, me);
			break;
		case R_AARCH64_LDST64_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 3, 9,
					     AARCH64_INSN_IMM_12, me);
			break;
		case R_AARCH64_LDST128_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 4, 8,
					     AARCH64_INSN_IMM_12, me);
			break;
		case R_AARCH64_TSTBR14:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2, 14,
					     AARCH64_INSN_IMM_14, me);
			break;
		case R_AARCH64_CONDBR19:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2, 19,
					     AARCH64_INSN_IMM_19, me);
			break;
		case R_AARCH64_JUMP26:
		case R_AARCH64_CALL26:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2, 26,
					     AARCH64_INSN_IMM_26, me);
			if (ovf == -ERANGE) {
				val = module_emit_plt_entry(me, sechdrs, loc, &rel[i], sym);
				if (!val)
					return -ENOEXEC;
				ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2,
						     26, AARCH64_INSN_IMM_26, me);
			}
			break;

		default:
			pr_err("module %s: unsupported RELA relocation: %llu\n",
			       me->name, ELF64_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}

		if (overflow_check && ovf == -ERANGE)
			goto overflow;

	}

	return 0;

overflow:
	pr_err("module %s: overflow in relocation type %d val %Lx\n",
	       me->name, (int)ELF64_R_TYPE(rel[i].r_info), val);
	return -ENOEXEC;
}

static inline void __init_plt(struct plt_entry *plt, unsigned long addr)
{
	*plt = get_plt_entry(addr, plt);
}

static int module_init_ftrace_plt(const Elf_Ehdr *hdr,
				  const Elf_Shdr *sechdrs,
				  struct module *mod)
{
#if defined(CONFIG_DYNAMIC_FTRACE)
	const Elf_Shdr *s;
	struct plt_entry *plts;

	s = find_section(hdr, sechdrs, ".text.ftrace_trampoline");
	if (!s)
		return -ENOEXEC;

	plts = (void *)s->sh_addr;

	__init_plt(&plts[FTRACE_PLT_IDX], FTRACE_ADDR);

	mod->arch.ftrace_trampolines = plts;
#endif
	return 0;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	const Elf_Shdr *s;
	int ret;

	s = find_section(hdr, sechdrs, ".altinstructions");
	if (s)
		apply_alternatives_module((void *)s->sh_addr, s->sh_size);

	if (scs_is_dynamic()) {
		s = find_section(hdr, sechdrs, ".init.eh_frame");
		if (s) {
			ret = __pi_scs_patch((void *)s->sh_addr, s->sh_size);
			if (ret)
				pr_err("module %s: error occurred during dynamic SCS patching (%d)\n",
				       me->name, ret);
		}
	}

	return module_init_ftrace_plt(hdr, sechdrs, me);
}
