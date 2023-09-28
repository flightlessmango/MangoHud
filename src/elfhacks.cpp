/**
 * \file src/elfhacks.c
 * \brief various ELF run-time hacks
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in elfhacks.h
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <elf.h>
#include <link.h>
#include <fnmatch.h>
#include "elfhacks.h"

#ifndef __ELF_NATIVE_CLASS
#include "sys/reg.h"
#define __ELF_NATIVE_CLASS __WORDSIZE
#endif

/**
 *  \addtogroup elfhacks
 *  \{
 */

#ifdef __GLIBC__
# define ABS_ADDR(obj, ptr) (ptr)
#else
# define ABS_ADDR(obj, ptr) ((obj->addr) + (ptr))
#endif

struct eh_iterate_callback_args {
	eh_iterate_obj_callback_func callback;
	void *arg;
};

int eh_check_addr(eh_obj_t *obj, const void *addr);
int eh_find_callback(struct dl_phdr_info *info, size_t size, void *argptr);
int eh_find_next_dyn(eh_obj_t *obj, ElfW_Sword tag, int i, ElfW(Dyn) **next);
int eh_init_obj(eh_obj_t *obj);

int eh_set_rela_plt(eh_obj_t *obj, int p, const char *sym, void *val);
int eh_set_rel_plt(eh_obj_t *obj, int p, const char *sym, void *val);

int eh_iterate_rela_plt(eh_obj_t *obj, int p, eh_iterate_rel_callback_func callback, void *arg);
int eh_iterate_rel_plt(eh_obj_t *obj, int p, eh_iterate_rel_callback_func callback, void *arg);
int eh_iterate_callback(struct dl_phdr_info *info, size_t size, void *argptr);

int eh_find_sym_hash(eh_obj_t *obj, const char *name, eh_sym_t *sym);
int eh_find_sym_gnu_hash(eh_obj_t *obj, const char *name, eh_sym_t *sym);

ElfW(Word) eh_hash_elf(const char *name);
Elf32_Word eh_hash_gnu(const char *name);

int eh_find_callback(struct dl_phdr_info *info, size_t size, void *argptr)
{
	eh_obj_t *find = (eh_obj_t *) argptr;

	if (find->name == NULL) {
		if (strcmp(info->dlpi_name, ""))
			return 0;
	} else if (fnmatch(find->name, info->dlpi_name, 0))
		return 0;

	if (find->name == NULL) /* TODO readlink? */
		find->name = "/proc/self/exe";
	else
		find->name = info->dlpi_name;
	find->addr = info->dlpi_addr;

	/* segment headers */
	find->phdr = info->dlpi_phdr;
	find->phnum = info->dlpi_phnum;

	return 0;
}

int eh_iterate_callback(struct dl_phdr_info *info, size_t size, void *argptr)
{
	struct eh_iterate_callback_args *args = (eh_iterate_callback_args *)argptr;
	eh_obj_t obj;
	int ret = 0;

	/* eh_init_obj needs phdr and phnum */
	obj.phdr = info->dlpi_phdr;
	obj.phnum = info->dlpi_phnum;
	obj.addr = info->dlpi_addr;
	obj.name = info->dlpi_name;

	if ((ret = eh_init_obj(&obj))) {
		if (ret == ENOTSUP) /* just skip */
			return 0;
		return ret;
	}

	if ((ret = args->callback(&obj, args->arg)))
		return ret;

	if ((ret = eh_destroy_obj(&obj)))
		return ret;

	return 0;
}

int eh_iterate_obj(eh_iterate_obj_callback_func callback, void *arg)
{
	int ret;
	struct eh_iterate_callback_args args;

	args.callback = callback;
	args.arg = arg;

	if ((ret = dl_iterate_phdr(eh_iterate_callback, &args)))
		return ret;

	return 0;
}

int eh_find_obj(eh_obj_t *obj, const char *soname)
{
	/* This function uses glibc-specific dl_iterate_phdr().
	   Another way could be parsing /proc/self/exe or using
	   pmap() on Solaris or *BSD */
	obj->phdr = NULL;
	obj->name = soname;
	dl_iterate_phdr(eh_find_callback, obj);

	if (!obj->phdr)
		return EAGAIN;

	return eh_init_obj(obj);
}

int eh_check_addr(eh_obj_t *obj, const void *addr)
{
	/*
	 Check that given address is inside program's
	 memory maps. PT_LOAD program headers tell us
	 where program has been loaded into.
	*/
	int p;
	for (p = 0; p < obj->phnum; p++) {
		if (obj->phdr[p].p_type == PT_LOAD) {
			if (((ElfW(Addr)) addr < obj->phdr[p].p_memsz + obj->phdr[p].p_vaddr + obj->addr) &&
			    ((ElfW(Addr)) addr >= obj->phdr[p].p_vaddr + obj->addr))
				return 0;
		}
	}

	return EINVAL;
}

int eh_init_obj(eh_obj_t *obj)
{
	/*
	 ELF spec says in section header documentation, that:
	 "An object file may have only one dynamic section."

	 Let's assume it means that object has only one PT_DYNAMIC
	 as well.
	*/
	int p;
	obj->dynamic = NULL;
	for (p = 0; p < obj->phnum; p++) {
		if (obj->phdr[p].p_type == PT_DYNAMIC) {
			if (obj->dynamic)
				return ENOTSUP;

			obj->dynamic = (ElfW(Dyn) *) (obj->phdr[p].p_vaddr + obj->addr);
		}
	}

	if (!obj->dynamic)
		return ENOTSUP;

	/*
	 ELF spec says that program is allowed to have more than one
	 .strtab but does not describe how string table indexes translate
	 to multiple string tables.

	 And spec says that only one SHT_HASH is allowed, does it mean that
	 obj has only one DT_HASH?

	 About .symtab it does not mention anything about if multiple
	 symbol tables are allowed or not.

	 Maybe st_shndx is the key here?
	*/
	obj->strtab = NULL;
	obj->hash = NULL;
	obj->gnu_hash = NULL;
	obj->symtab = NULL;
	p = 0;
	while (obj->dynamic[p].d_tag != DT_NULL) {
		if (obj->dynamic[p].d_tag == DT_STRTAB) {
			if (obj->strtab)
				return ENOTSUP;

			obj->strtab = (const char *) ABS_ADDR(obj, obj->dynamic[p].d_un.d_ptr);
		} else if (obj->dynamic[p].d_tag == DT_HASH) {
			if (obj->hash)
				return ENOTSUP;

			obj->hash = (ElfW(Word) *) ABS_ADDR(obj, obj->dynamic[p].d_un.d_ptr);
		} else if (obj->dynamic[p].d_tag == DT_GNU_HASH) {
			if (obj->gnu_hash)
				return ENOTSUP;

			obj->gnu_hash = (Elf32_Word *) ABS_ADDR(obj, obj->dynamic[p].d_un.d_ptr);
		} else if (obj->dynamic[p].d_tag == DT_SYMTAB) {
			if (obj->symtab)
				return ENOTSUP;

			obj->symtab = (ElfW(Sym) *) ABS_ADDR(obj, obj->dynamic[p].d_un.d_ptr);
		}
		p++;
	}

	/* This is here to catch b0rken headers (vdso) */
	if ((eh_check_addr(obj, (const void *) obj->strtab)) |
	    (eh_check_addr(obj, (const void *) obj->symtab)))
		return ENOTSUP;

	if (obj->hash) {
		/* DT_HASH found */
		if (eh_check_addr(obj, (void *) obj->hash))
			obj->hash = NULL;
	} else if (obj->gnu_hash) {
		/* DT_GNU_HASH found */
		if (eh_check_addr(obj, (void *) obj->gnu_hash))
			obj->gnu_hash = NULL;
	}

	return 0;
}

int eh_find_sym(eh_obj_t *obj, const char *name, void **to)
{
	eh_sym_t sym;

	/* DT_GNU_HASH is faster ;) */
	if (obj->gnu_hash) {
		if (!eh_find_sym_gnu_hash(obj, name, &sym)) {
			*to = (void *) (sym.sym->st_value + obj->addr);
			return 0;
		}
	}

	/* maybe it is in DT_HASH or DT_GNU_HASH is not present */
	if (obj->hash) {
		if (!eh_find_sym_hash(obj, name, &sym)) {
			*to = (void *) (sym.sym->st_value + obj->addr);
			return 0;
		}
	}

	return EAGAIN;
}

ElfW(Word) eh_hash_elf(const char *name)
{
	ElfW(Word) tmp, hash = 0;
	const unsigned char *uname = (const unsigned char *) name;
	int c;

	while ((c = *uname++) != '\0') {
		hash = (hash << 4) + c;
		if ((tmp = (hash & 0xf0000000)) != 0) {
			hash ^= tmp >> 24;
			hash ^= tmp;
		}
	}

	return hash;
}

int eh_find_sym_hash(eh_obj_t *obj, const char *name, eh_sym_t *sym)
{
	ElfW(Word) hash, *chain;
	ElfW(Sym) *esym;
	unsigned int bucket_idx, idx;

	if (!obj->hash)
		return ENOTSUP;

	if (obj->hash[0] == 0)
		return EAGAIN;

	hash = eh_hash_elf(name);
	/*
	 First item in DT_HASH is nbucket, second is nchain.
	 hash % nbucket gives us our bucket index.
	*/
	bucket_idx = obj->hash[2 + (hash % obj->hash[0])];
	chain = &obj->hash[2 + obj->hash[0] + bucket_idx];

	idx = 0;
	sym->sym = NULL;

	/* we have to check symtab[bucket_idx] first */
	esym = &obj->symtab[bucket_idx];
	if (esym->st_name) {
		if (!strcmp(&obj->strtab[esym->st_name], name))
			sym->sym = esym;
	}

	while ((sym->sym == NULL) &&
	       (chain[idx] != STN_UNDEF)) {
		esym = &obj->symtab[chain[idx]];

		if (esym->st_name) {
			if (!strcmp(&obj->strtab[esym->st_name], name))
				sym->sym = esym;
		}

		idx++;
	}

	/* symbol not found */
	if (sym->sym == NULL)
		return EAGAIN;

	sym->obj = obj;
	sym->name = &obj->strtab[sym->sym->st_name];

	return 0;
}

Elf32_Word eh_hash_gnu(const char *name)
{
	Elf32_Word hash = 5381;
	const unsigned char *uname = (const unsigned char *) name;
	int c;

	while ((c = *uname++) != '\0')
		hash = (hash << 5) + hash + c;

	return hash & 0xffffffff;
}

int eh_find_sym_gnu_hash(eh_obj_t *obj, const char *name, eh_sym_t *sym)
{
	Elf32_Word *buckets, *chain_zero, *hasharr;
	ElfW(Addr) *bitmask, bitmask_word;
	Elf32_Word symbias, bitmask_nwords, bucket,
		   nbuckets, bitmask_idxbits, shift;
	Elf32_Word hash, hashbit1, hashbit2;
	ElfW(Sym) *esym;

	if (!obj->gnu_hash)
		return ENOTSUP;

	if (obj->gnu_hash[0] == 0)
		return EAGAIN;

	sym->sym = NULL;

	/*
	 Initialize our hash table stuff

	 DT_GNU_HASH is(?):
	 [nbuckets] [symbias] [bitmask_nwords] [shift]
	 [bitmask_nwords * ElfW(Addr)] <- bitmask
	 [nbuckets * Elf32_Word] <- buckets
	 ...chains? - symbias...
	 */
	nbuckets = obj->gnu_hash[0];
	symbias = obj->gnu_hash[1];
	bitmask_nwords = obj->gnu_hash[2]; /* must be power of two */
	bitmask_idxbits = bitmask_nwords - 1;
	shift = obj->gnu_hash[3];
	bitmask = (ElfW(Addr) *) &obj->gnu_hash[4];
	buckets = &obj->gnu_hash[4 + (__ELF_NATIVE_CLASS / 32) * bitmask_nwords];
	chain_zero = &buckets[nbuckets] - symbias;

	/* hash our symbol */
	hash = eh_hash_gnu(name);

	/* bitmask stuff... no idea really :D */
	bitmask_word = bitmask[(hash / __ELF_NATIVE_CLASS) & bitmask_idxbits];
	hashbit1 = hash & (__ELF_NATIVE_CLASS - 1);
	hashbit2 = (hash >> shift) & (__ELF_NATIVE_CLASS - 1);

	/* wtf this does actually? */
	if (!((bitmask_word >> hashbit1) & (bitmask_word >> hashbit2) & 1))
		return EAGAIN;

	/* locate bucket */
	bucket = buckets[hash % nbuckets];
	if (bucket == 0)
		return EAGAIN;

	/* and find match in chain */
	hasharr = &chain_zero[bucket];
	do {
		if (((*hasharr ^ hash) >> 1) == 0) {
			/* hash matches, but does the name? */
			esym = &obj->symtab[hasharr - chain_zero];
			if (esym->st_name) {
				if (!strcmp(&obj->strtab[esym->st_name], name)) {
					sym->sym = esym;
					break;
				}
			}
		}
	} while ((*hasharr++ & 1u) == 0);

	/* symbol not found */
	if (sym->sym == NULL)
		return EAGAIN;

	sym->obj = obj;
	sym->name = &obj->strtab[sym->sym->st_name];

	return 0;
}

int eh_iterate_sym(eh_obj_t *obj, eh_iterate_sym_callback_func callback, void *arg)
{
	return ENOTSUP;
}

int eh_find_next_dyn(eh_obj_t *obj, ElfW_Sword tag, int i, ElfW(Dyn) **next)
{
	/* first from i + 1 to end, then from start to i - 1 */
	int p;
	*next = NULL;

	p = i + 1;
	while (obj->dynamic[p].d_tag != DT_NULL) {
		if (obj->dynamic[p].d_tag == tag) {
			*next = &obj->dynamic[p];
			return 0;
		}
		p++;
	}

	p = 0;
	while ((obj->dynamic[i].d_tag != DT_NULL) && (p < i)) {
		if (obj->dynamic[p].d_tag == tag) {
			*next = &obj->dynamic[p];
			return 0;
		}
		p++;
	}

	return EAGAIN;
}

int eh_set_rela_plt(eh_obj_t *obj, int p, const char *sym, void *val)
{
	ElfW(Rela) *rela = (ElfW(Rela) *) ABS_ADDR(obj, obj->dynamic[p].d_un.d_ptr);
	ElfW(Dyn) *relasize;
	unsigned int i;

	/* DT_PLTRELSZ contains PLT relocs size in bytes */
	if (eh_find_next_dyn(obj, DT_PLTRELSZ, p, &relasize))
		return EINVAL; /* b0rken elf :/ */

	for (i = 0; i < relasize->d_un.d_val / sizeof(ElfW(Rela)); i++) {
		if (!obj->symtab[ELFW_R_SYM(rela[i].r_info)].st_name)
			continue;

		if (!strcmp(&obj->strtab[obj->symtab[ELFW_R_SYM(rela[i].r_info)].st_name], sym))
			*((void **) (rela[i].r_offset + obj->addr)) = val;
	}

	return 0;
}

int eh_set_rel_plt(eh_obj_t *obj, int p, const char *sym, void *val)
{
	ElfW(Rel) *rel = (ElfW(Rel) *) ABS_ADDR(obj, obj->dynamic[p].d_un.d_ptr);
	ElfW(Dyn) *relsize;
	unsigned int i;

	if (eh_find_next_dyn(obj, DT_PLTRELSZ, p, &relsize))
		return EINVAL; /* b0rken elf :/ */

	for (i = 0; i < relsize->d_un.d_val / sizeof(ElfW(Rel)); i++) {
		if (!obj->symtab[ELFW_R_SYM(rel[i].r_info)].st_name)
			continue;

		if (!strcmp(&obj->strtab[obj->symtab[ELFW_R_SYM(rel[i].r_info)].st_name], sym))
			*((void **) (rel[i].r_offset + obj->addr)) = val;
	}

	return 0;
}

int eh_set_rel(eh_obj_t *obj, const char *sym, void *val)
{
	/*
	 Elf spec states that object is allowed to have multiple
	 .rel.plt and .rela.plt tables, so we will support 'em - here.
	*/
	ElfW(Dyn) *pltrel;
	int ret, p = 0;

	while (obj->dynamic[p].d_tag != DT_NULL) {
		/* DT_JMPREL contains .rel.plt or .rela.plt */
		if (obj->dynamic[p].d_tag == DT_JMPREL) {
			/* DT_PLTREL tells if it is Rela or Rel */
			eh_find_next_dyn(obj, DT_PLTREL, p, &pltrel);

			if (pltrel->d_un.d_val == DT_RELA) {
				if ((ret = eh_set_rela_plt(obj, p, sym, val)))
					return ret;
			} else if (pltrel->d_un.d_val == DT_REL) {
				if ((ret = eh_set_rel_plt(obj, p, sym, val)))
					return ret;
			} else
				return EINVAL;
		}
		p++;
	}

	return 0;
}

int eh_iterate_rela_plt(eh_obj_t *obj, int p, eh_iterate_rel_callback_func callback, void *arg)
{
	ElfW(Rela) *rela = (ElfW(Rela) *) ABS_ADDR(obj, obj->dynamic[p].d_un.d_ptr);
	ElfW(Dyn) *relasize;
	eh_rel_t rel;
	eh_sym_t sym;
	unsigned int i, ret;

	rel.sym = &sym;
	rel.rel = NULL;
	rel.obj = obj;

	if (eh_find_next_dyn(obj, DT_PLTRELSZ, p, &relasize))
		return EINVAL;

	for (i = 0; i < relasize->d_un.d_val / sizeof(ElfW(Rela)); i++) {
		rel.rela = &rela[i];
		sym.sym = &obj->symtab[ELFW_R_SYM(rel.rela->r_info)];
		if (sym.sym->st_name)
			sym.name = &obj->strtab[sym.sym->st_name];
		else
			sym.name = NULL;

		if ((ret = callback(&rel, arg)))
			return ret;
	}

	return 0;
}

int eh_iterate_rel_plt(eh_obj_t *obj, int p, eh_iterate_rel_callback_func callback, void *arg)
{
	ElfW(Rel) *relp = (ElfW(Rel) *) ABS_ADDR(obj, obj->dynamic[p].d_un.d_ptr);
	ElfW(Dyn) *relsize;
	eh_rel_t rel;
	eh_sym_t sym;
	unsigned int i, ret;

	rel.sym = &sym;
	rel.rela = NULL;
	rel.obj = obj;

	if (eh_find_next_dyn(obj, DT_PLTRELSZ, p, &relsize))
		return EINVAL;

	for (i = 0; i < relsize->d_un.d_val / sizeof(ElfW(Rel)); i++) {
		rel.rel = &relp[i];
		sym.sym = &obj->symtab[ELFW_R_SYM(rel.rel->r_info)];
		if (sym.sym->st_name)
			sym.name = &obj->strtab[sym.sym->st_name];
		else
			sym.name = NULL;

		if ((ret = callback(&rel, arg)))
			return ret;
	}

	return 0;
}

int eh_iterate_rel(eh_obj_t *obj, eh_iterate_rel_callback_func callback, void *arg)
{
	ElfW(Dyn) *pltrel;
	int ret, p = 0;

	while (obj->dynamic[p].d_tag != DT_NULL) {
		if (obj->dynamic[p].d_tag == DT_JMPREL) {
			eh_find_next_dyn(obj, DT_PLTREL, p, &pltrel);

			if (pltrel->d_un.d_val == DT_RELA) {
				if ((ret = eh_iterate_rela_plt(obj, p, callback, arg)))
					return ret;
			} else if (pltrel->d_un.d_val == DT_REL) {
				if ((ret = eh_iterate_rel_plt(obj, p, callback, arg)))
					return ret;
			} else
				return EINVAL;
		}
		p++;
	}

	return 0;
}

int eh_destroy_obj(eh_obj_t *obj)
{
	obj->phdr = NULL;

	return 0;
}

/**  \} */
