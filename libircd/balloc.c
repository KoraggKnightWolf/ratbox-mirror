/*
 *  ircd-ratbox: A slightly useful ircd.
 *  balloc.c: A block allocator.
 *
 * Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 * Copyright (C) 1996-2002 Hybrid Development Team
 * Copyright (C) 2002-2006 ircd-ratbox development team
 *
 *  Below are the orignal headers from the old blalloc.c 
 *
 *  File:   blalloc.c
 *  Owner:  Wohali (Joan Touzet)
 *  
 *  Modified 2001/11/29 for mmap() support by Aaron Sethman <androsyn@ratbox.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 *  $Id$
 */

/* 
 * About the block allocator
 *
 * Basically we have three ways of getting memory off of the operating
 * system. Below are this list of methods and the order of preference.
 *
 * 1. mmap() anonymous pages with the MMAP_ANON flag.
 * 2. mmap() via the /dev/zero trick.
 * 3. HeapCreate/HeapAlloc (on win32) 
 * 4. malloc() 
 *
 * The advantages of 1 and 2 are this.  We can munmap() the pages which will
 * return the pages back to the operating system, thus reducing the size 
 * of the process as the memory is unused.  malloc() on many systems just keeps
 * a heap of memory to itself, which never gets given back to the OS, except on
 * exit.  This of course is bad, if say we have an event that causes us to allocate
 * say, 200MB of memory, while our normal memory consumption would be 15MB.  In the
 * malloc() case, the amount of memory allocated to our process never goes down, as
 * malloc() has it locked up in its heap.  With the mmap() method, we can munmap()
 * the block and return it back to the OS, thus causing our memory consumption to go
 * down after we no longer need it.
 * 
 *
 *
 */
#include "ircd_lib.h"
#include "balloc.h"
#ifndef NOBALLOC

#include "tools.h"
#include "event.h"

#ifdef HAVE_MMAP		/* We've got mmap() that is good */
#include <sys/mman.h>
/* HP-UX sucks */
#ifdef MAP_ANONYMOUS
#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif
#endif

static int newblock(ircd_bh * bh);
static void ircd_bh_gc_event(void *unused);
static dlink_list heap_lists;

#if defined(HAVE_MMAP) && !defined(MAP_ANON)
static int zero_fd = -1;
#endif

#if defined(_WIN32)
static HANDLE block_heap;
#endif

#define ircd_bh_fail(x) _ircd_bh_fail(x, __FILE__, __LINE__)

static void
_ircd_bh_fail(const char *reason, const char *file, int line)
{
	ircd_lib_log("ircd_heap_blockheap failure: %s (%s:%d)", reason, file, line);
	abort();
}
                

/*
 * static inline void free_block(void *ptr, size_t size)
 *
 * Inputs: The block and its size
 * Output: None
 * Side Effects: Returns memory for the block back to the OS
 */
static inline void
free_block(void *ptr, size_t size)
{
#ifdef HAVE_MMAP
	munmap(ptr, size);
#else
#ifdef _WIN32
	HeapFree(block_heap, 0, ptr);
#else
	free(ptr);
#endif
#endif
}


#if defined(HAVE_MMAP) && !defined(MAP_ANON)
void
reopen_devzero(void)
{
	zero_fd = open("/dev/zero", O_RDWR);
	if(zero_fd < 0)
		ircd_bh_fail("Failed opening /dev/zero");
}

#endif

/*
 * void ircd_init_bh(void)
 * 
 * Inputs: None
 * Outputs: None
 * Side Effects: Initializes the block heap
 */

void
ircd_init_bh(void)
{
#if defined(HAVE_MMAP) && !defined(MAP_ANON)
	reopen_devzero();
#else
#ifdef _WIN32
 	block_heap = HeapCreate(HEAP_NO_SERIALIZE, 0, 0);	
#endif
#endif
	ircd_event_addish("ircd_bh_gc_event", ircd_bh_gc_event, NULL, 120);
}

/*
 * static inline void *get_block(size_t size)
 * 
 * Input: Size of block to allocate
 * Output: Pointer to new block
 * Side Effects: None
 */
static inline void *
get_block(size_t size)
{
	void *ptr;
#ifdef HAVE_MMAP
#ifdef MAP_ANON
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#else
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, zero_fd, 0);
#endif /* MAP_ANON */
	if(ptr == MAP_FAILED)
	{
		ptr = NULL;		
#ifndef MAP_ANON
		if(errno == EBADF) {
			reopen_devzero();
			ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, zero_fd, 0);
			if(ptr == MAP_FAILED)
				ptr = NULL;
		}
#endif
	}
#else
#ifdef _WIN32
	ptr = HeapAlloc(block_heap, 0, size);
#else 
	ptr = malloc(size);
#endif
#endif
	return(ptr);
}


static void
ircd_bh_gc_event(void *unused)
{
	dlink_node *ptr;
	DLINK_FOREACH(ptr, heap_lists.head)
	{
		ircd_bh_gc(ptr->data);
	}
}

/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    newblock                                                              */
/* Description:                                                             */
/*    Allocates a new block for addition to a blockheap                     */
/* Parameters:                                                              */
/*    bh (IN): Pointer to parent blockheap.                                 */
/* Returns:                                                                 */
/*    0 if successful, 1 if not                                             */
/* ************************************************************************ */

static int
newblock(ircd_bh * bh)
{
	ircd_heap_block *b;
	unsigned long i;
	void *offset;

	/* Setup the initial data structure. */
	b = ircd_malloc(sizeof(ircd_heap_block));

	b->alloc_size = (bh->elemsPerBlock + 1) * (bh->elemSize + sizeof(ircd_heap_memblock));

	b->elems = get_block(b->alloc_size);
	if(unlikely(b->elems == NULL))
	{
		return (1);
	}
	offset = b->elems;
	/* Setup our blocks now */
	for (i = 0; i < bh->elemsPerBlock; i++, offset += (bh->elemSize + sizeof(ircd_heap_block *)))
	{
		ircd_heap_memblock *memblock = (ircd_heap_memblock *)offset;
		memblock->block = b;
		ircd_dlinkAdd(memblock, &memblock->ndata.node, &bh->free_list);
	}
	ircd_dlinkAdd(b, &b->node, &bh->block_list);
	b->free_count = bh->elemsPerBlock;	
	return (0);
}


/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    ircd_bh_create                                                       */
/* Description:                                                             */
/*   Creates a new blockheap from which smaller blocks can be allocated.    */
/*   Intended to be used instead of multiple calls to malloc() when         */
/*   performance is an issue.                                               */
/* Parameters:                                                              */
/*   elemsize (IN):  Size of the basic element to be stored                 */
/*   elemsperblock (IN):  Number of elements to be stored in a single block */
/*         of memory.  When the blockheap runs out of free memory, it will  */
/*         allocate elemsize * elemsperblock more.                          */
/* Returns:                                                                 */
/*   Pointer to new ircd_bh, or NULL if unsuccessful                      */
/* ************************************************************************ */
ircd_bh *
ircd_bh_create(size_t elemsize, int elemsperblock)
{
	ircd_bh *bh;
	lircd_assert(elemsize > 0 && elemsperblock > 0);
	lircd_assert(elemsize >= sizeof(dlink_node));
	/* Catch idiotic requests up front */
	if((elemsize <= 0) || (elemsperblock <= 0))
	{
		ircd_bh_fail("Attempting to ircd_bh_create idiotic sizes");
	}

	if(elemsize < sizeof(dlink_node))
		ircd_bh_fail("Attempt to ircd_bh_create smaller than sizeof(dlink_node)");
	
	/* Allocate our new ircd_bh */
	bh = ircd_malloc(sizeof(ircd_bh));

	if((elemsize % sizeof(void *)) != 0)
	{
		/* Pad to even pointer boundary */
		elemsize += sizeof(void *);
		elemsize &= ~(sizeof(void *) - 1);
	}

	bh->elemSize = elemsize;
	bh->elemsPerBlock = elemsperblock;

	/* Be sure our malloc was successful */
	if(newblock(bh))
	{
		if(bh != NULL)
			free(bh);
		ircd_lib_log("newblock() failed");
		ircd_outofmemory();	/* die.. out of memory */
	}

	if(bh == NULL)
	{
		ircd_bh_fail("bh == NULL when it shouldn't be");
	}
	ircd_dlinkAdd(bh, &bh->hlist, &heap_lists);
	return (bh);
}

/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    ircd_bh_alloc                                                        */
/* Description:                                                             */
/*    Returns a pointer to a struct within our ircd_bh that's free for    */
/*    the taking.                                                           */
/* Parameters:                                                              */
/*    bh (IN):  Pointer to the Blockheap.                                   */
/* Returns:                                                                 */
/*    Pointer to a structure (void *), or NULL if unsuccessful.             */
/* ************************************************************************ */

void *
ircd_bh_alloc(ircd_bh * bh)
{
	dlink_node *new_node;
	ircd_heap_memblock *memblock;
	
	lircd_assert(bh != NULL);
	if(unlikely(bh == NULL))
	{
		ircd_bh_fail("Cannot allocate if bh == NULL");
	}

	if(bh->free_list.head == NULL)
	{
		/* Allocate new block and assign */
		/* newblock returns 1 if unsuccessful, 0 if not */

		if(unlikely(newblock(bh)))
		{
			ircd_lib_log("newblock() failed");
			ircd_outofmemory();	/* Well that didn't work either...bail */
		}
		if(bh->free_list.head == NULL)
		{
			ircd_lib_log("out of memory after newblock()...");
			ircd_outofmemory();
		}
	}

	new_node = bh->free_list.head;
	memblock = new_node->data;
	ircd_dlinkDelete(new_node, &bh->free_list);
	memblock->block->free_count--;
	memset((void *)memblock->ndata.data, 0, bh->elemSize);
	return((void *)memblock->ndata.data);	
}


/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    ircd_bh_free                                                          */
/* Description:                                                             */
/*    Returns an element to the free pool, does not free()                  */
/* Parameters:                                                              */
/*    bh (IN): Pointer to ircd_bh containing element                        */
/*    ptr (in):  Pointer to element to be "freed"                           */
/* Returns:                                                                 */
/*    0 if successful, 1 if element not contained within ircd_bh.           */
/* ************************************************************************ */
int
ircd_bh_free(ircd_bh * bh, void *ptr)
{
	ircd_heap_memblock *memblock;
	dlink_node *node;
	lircd_assert(bh != NULL);
	lircd_assert(ptr != NULL);

	if(unlikely(bh == NULL))
	{
		ircd_lib_log("balloc.c:ircd_bhFree() bh == NULL");
		return (1);
	}

	if(unlikely(ptr == NULL))
	{
		ircd_lib_log("balloc.ircd_bhFree() ptr == NULL");
		return (1);
	}
	
	node = ptr;
	memblock = (ircd_heap_memblock *) (ptr - sizeof(ircd_heap_block *));
	memblock->block->free_count++;
	memset(node, 0, sizeof(dlink_node));
	ircd_dlinkAdd(memblock, node, &bh->free_list);
	return (0);
}


/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    ircd_bhDestroy                                                      */
/* Description:                                                             */
/*    Completely free()s a ircd_bh.  Use for cleanup.                     */
/* Parameters:                                                              */
/*    bh (IN):  Pointer to the ircd_bh to be destroyed.                   */
/* Returns:                                                                 */
/*   0 if successful, 1 if bh == NULL                                       */
/* ************************************************************************ */
int
ircd_bh_destroy(ircd_bh * bh)
{
	dlink_node *ptr, *next;
	ircd_heap_block *b;

	if(bh == NULL)
		return (1);

	DLINK_FOREACH_SAFE(ptr, next, bh->block_list.head)
	{
		b = ptr->data;
		free_block(b->elems, b->alloc_size);
		ircd_free(b);
	}
	
	ircd_dlinkDelete(&bh->hlist, &heap_lists);
	ircd_free(bh);

	return (0);
}

void
ircd_bh_usage(ircd_bh * bh, size_t * bused, size_t * bfree, size_t * bmemusage)
{
	size_t used;
	size_t freem;
	size_t memusage;
	if(bh == NULL)
	{
		return;
	}

	freem = ircd_dlink_list_length(&bh->free_list);
	used = (ircd_dlink_list_length(&bh->block_list) * bh->elemsPerBlock) - freem;
	memusage = used * (bh->elemSize + sizeof(void *));
	if(bused != NULL)
		*bused = used;
	if(bfree != NULL)
		*bfree = freem;
	if(bmemusage != NULL)
		*bmemusage = memusage;
}

int
ircd_bh_gc(ircd_bh * bh)
{
	ircd_heap_block *b;
	dlink_node *ptr, *next;
	unsigned long i;
	
	if(bh == NULL)
	{
		/* somebody is smoking some craq..(probably lee, but don't tell him that) */
		return (1);
	}

	if((ircd_dlink_list_length(&bh->free_list) < bh->elemsPerBlock) || ircd_dlink_list_length(&bh->block_list) == 1)
	{
		/* There couldn't possibly be an entire free block.  Return. */
		return (0);
	}

	DLINK_FOREACH_SAFE(ptr, next, bh->block_list.head)
	{
		b = ptr->data;
		if(ircd_dlink_list_length(&bh->block_list) == 1)
			return (0);
		
		if(b->free_count == bh->elemsPerBlock)
		{
			/* i'm seriously going to hell for this.. */
			void *offset;

			offset = b->elems;
			for (i = 0; i < bh->elemsPerBlock; i++, offset += (bh->elemSize + sizeof(ircd_heap_block *)))
			{
				ircd_heap_memblock *memblock = (ircd_heap_memblock *)offset;
				ircd_dlinkDelete(&memblock->ndata.node, &bh->free_list);
			}
			ircd_dlinkDelete(&b->node, &bh->block_list);
			free_block(b->elems, b->alloc_size);
			ircd_free(b);
		}
		
	}
	return (0);
}


#endif /* NOBALLOC */

