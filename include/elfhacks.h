/**
 * \file src/elfhacks.h
 * \brief elfhacks application interface
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 */

/* elfhacks.h -- Various ELF run-time hacks
  version 0.4.1, March 9th, 2008

  Copyright (C) 2007-2008 Pyry Haulos

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Pyry Haulos <pyry.haulos@gmail.com>
*/

#include <elf.h>
#include <link.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __PUBLIC __attribute__ ((visibility ("default")))

#ifdef __x86_64__
# define __elf64
#endif
#ifdef __i386__
# define __elf32
#endif

#ifdef __elf64
# define ELFW_R_SYM ELF64_R_SYM
# define ElfW_Sword Elf64_Sxword
#else
# ifdef __elf32
#  define ELFW_R_SYM ELF32_R_SYM
#  define ElfW_Sword Elf32_Sword
# else
#  error neither __elf32 nor __elf64 is defined
# endif
#endif

/**
 *  \defgroup elfhacks elfhacks
 *  Elfhacks is a collection of functions that aim for retvieving
 *  or modifying progam's dynamic linking information at run-time.
 *  \{
 */

/**
 * \brief elfhacks program object
 */
typedef struct {
	/** file name */
	const char *name;
	/** base address in memory */
	ElfW(Addr) addr;
	/** program headers */
	const ElfW(Phdr) *phdr;
	/** number of program headers */
	ElfW(Half) phnum;
	/** .dynamic */
	ElfW(Dyn) *dynamic;
	/** .symtab */
	ElfW(Sym) *symtab;
	/** .strtab */
	const char *strtab;
	/** symbol hash table (DT_HASH) */
	ElfW(Word) *hash;
	/** symbol hash table (DT_GNU_HASH) */
	Elf32_Word *gnu_hash;
} eh_obj_t;

/**
 * \brief elfhacks symbol
 */
typedef struct {
	/** symbol name */
	const char *name;
	/** corresponding ElfW(Sym) */
	ElfW(Sym) *sym;
	/** elfhacks object this symbol is associated to */
	eh_obj_t *obj;
} eh_sym_t;

/**
 * \brief elfhacks relocation
 */
typedef struct {
	/** symbol this relocation is associated to */
	eh_sym_t *sym;
	/** corresponding ElfW(Rel) (NULL if this is Rela) */
	ElfW(Rel) *rel;
	/** corresponding ElfW(Rela) (NULL if this is Rel) */
	ElfW(Rela) *rela;
	/** elfhacks program object */
	eh_obj_t *obj;
} eh_rel_t;

/**
 * \brief Iterate objects callback
 */
typedef int (*eh_iterate_obj_callback_func)(eh_obj_t *obj, void *arg);
/**
 * \brief Iterate symbols callback
 */
typedef int (*eh_iterate_sym_callback_func)(eh_sym_t *sym, void *arg);
/**
 * \brief Iterate relocations callback
 */
typedef int (*eh_iterate_rel_callback_func)(eh_rel_t *rel, void *arg);

/**
 * \brief Initializes eh_obj_t for given soname
 *
 * Matching is done using fnmatch() so wildcards and other standard
 * filename metacharacters and expressions work.
 *
 * If soname is NULL, this function returns the main program object.
 * \param obj elfhacks object
 * \param soname object's soname (see /proc/pid/maps) or NULL for main
 * \return 0 on success otherwise a positive error code
*/
__PUBLIC int eh_find_obj(eh_obj_t *obj, const char *soname);

/**
 * \brief Walk through list of objects
 * \param callback callback function
 * \param arg argument passed to callback function
 * \return 0 on success otherwise an error code
 */
__PUBLIC int eh_iterate_obj(eh_iterate_obj_callback_func callback, void *arg);

/**
 * \brief Finds symbol in object's .dynsym and retrvieves its value.
 * \param obj elfhacks program object
 * \param name symbol to find
 * \param to returned value
 * \return 0 on success otherwise a positive error code
*/
__PUBLIC int eh_find_sym(eh_obj_t *obj, const char *name, void **to);

/**
 * \brief Walk through list of symbols in object
 * \param obj elfhacks program object
 * \param callback callback function
 * \param arg argument passed to callback function
 * \return 0 on success otherwise an error code
 */
__PUBLIC int eh_iterate_sym(eh_obj_t *obj, eh_iterate_sym_callback_func callback, void *arg);

/**
 * \brief Iterates through object's .rel.plt and .rela.plt and sets every
 *        occurrence of some symbol to the specified value.
 * \param obj elfhacks program object
 * \param sym symbol to replace
 * \param val new value
 * \return 0 on success otherwise a positive error code
*/
__PUBLIC int eh_set_rel(eh_obj_t *obj, const char *sym, void *val);

/**
 * \brief Walk through object's .rel.plt and .rela.plt
 * \param obj elfhacks program object
 * \param callback callback function
 * \param arg argument passed to callback function
 */
__PUBLIC int eh_iterate_rel(eh_obj_t *obj, eh_iterate_rel_callback_func callback, void *arg);

/**
 * \brief Destroy eh_obj_t object.
 * \param obj elfhacks program object
 * \return 0 on success otherwise a positive error code
*/
__PUBLIC int eh_destroy_obj(eh_obj_t *obj);

/** \} */

#ifdef __cplusplus
}
#endif
