/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _CAM_MEM_MGR_API_H_
#define _CAM_MEM_MGR_API_H_

#include <media/cam_req_mgr.h>
#include "cam_smmu_api.h"

extern bool mem_trace_en;

/**
 * CAM_MEM_ALLOC : Allocates memory without initializing it to zero.
 *
 * @size    : size of memory requested for allocation
 * @flags   : GFP flags (GFP_KERNEL, GFP_DMA, GFP_ATOMIC etc)
 *
 * NOTE: GFP_ATOMIC is not supported by vmalloc, so if allocation
 * fails with kmalloc for GFP_ATOMIC, then fallback option won't
 * be present.
 */
#define CAM_MEM_ALLOC(size, flags) \
	kvmalloc(size, flags)

/**
 * CAM_MEM_ZALLOC : Allocates memory and zero initialize it.
 *
 * @size    : size of memory requested for allocation
 * @flags   : GFP flags (GPF_KERNEL,  GFP_DMA, GPF_ATOMIC etc)
 *
 * NOTE: GFP_ATOMIC is not supported by vmalloc, so if allocation
 * fails with kmalloc for GFP_ATOMIC, then fallback option won't
 * be present.
 */
#define CAM_MEM_ZALLOC(size, flags) \
	(mem_trace_en ? \
	cam_mem_trace_alloc(size, flags, __func__, __LINE__) :\
	kvzalloc(size, flags))

/**
 * CAM_MEM_ZALLOC_ARRAY : Allocates memory for array and zero initialises it.
 *
 * @count   : count of number of elements in array
 * @size    : size of each element
 * @flags   : GFP flags (GFP_KERNEL, GFP_DMA, GFP_ATOMIC etc)
 *
 * NOTE: GFP_ATOMIC is not supported by vmalloc, so if allocation
 * fails with kmalloc for GFP_ATOMIC, then fallback option won't
 * be present.
 */
#define CAM_MEM_ZALLOC_ARRAY(count, size, flags) \
	(mem_trace_en ? \
	CAM_MEM_ZALLOC(count * size, flags) :\
	kvcalloc(count, size, flags))

/**
 * CAM_MEM_FREE : Frees memory without initializing it to zero.
 *
 * @addr    : address of data object to be freed
 */
#define CAM_MEM_FREE(addr) \
	(mem_trace_en ? \
	cam_mem_trace_free(addr) :\
	kvfree(addr))

/**
 * CAM_MEM_ZFREE : Frees memory and zero initialize it.
 *
 * @addr   : address of data object to be freed
 * @size   : size of the data object
 */
#define CAM_MEM_ZFREE(addr, size) \
	if (likely(!ZERO_OR_NULL_PTR(addr))) { \
		memset((void *)addr, 0x0,  size); \
		mem_trace_en ? \
		cam_mem_trace_free(addr) :\
		kvfree(addr); \
	}

/**
 * CAM_MEMDUP_USER : Allocates memory and copy from userspace
 * see memdup_user in linux kernel
 */
#define CAM_MEMDUP_USER(src, len) \
	(mem_trace_en ? \
	memdup_user_trace(src, len, __func__, __LINE__) :\
	memdup_user(src, len))

/**
 * Slab cache allocation helpers
 *
 * These macros wrap the kernel kmem_cache_*() API. Use them instead of
 * kmalloc/kfree when the same fixed-size object is allocated and freed
 * repeatedly on a hot path. Benefits over kmalloc:
 *
 *   - No size round-up: the slab allocator serves objects at their exact size,
 *     avoiding the power-of-two waste of the general-purpose heap.
 *   - Faster alloc/free: a per-CPU magazine of pre-allocated objects avoids
 *     the global heap lock on the common case.
 *   - Reduced fragmentation: objects of the same size are grouped together.
 *
 * NOTE: For variable-size or infrequent allocations use CAM_MEM_ALLOC instead.
 */

/**
 * CAM_MEM_KMEM_CACHE_CREATE - Create a slab cache.
 *
 * Wraps kmem_cache_create().
 *
 * @_name  : name string for the cache (used in /proc/slabinfo)
 * @_size  : size of each object in the cache
 * @_align : required alignment for objects (0 = default)
 * @_flags : SLAB_* flags (e.g. SLAB_CONSISTENCY_CHECKS)
 * @_ctor  : optional object constructor callback, or NULL
 *
 * Returns a struct kmem_cache * on success, or NULL on failure.
 */
#define CAM_MEM_KMEM_CACHE_CREATE(_name, _size, _align, _flags, _ctor) \
	kmem_cache_create((_name), (_size), (_align), (_flags), (_ctor))

/**
 * CAM_MEM_KMEM_CACHE_ZALLOC - Allocate a zeroed object from a slab cache.
 *
 * Wraps kmem_cache_zalloc() with GFP_KERNEL.
 *
 * @_cache : struct kmem_cache * to allocate from
 *
 * Returns a pointer to the allocated object, or NULL on failure.
 */
#define CAM_MEM_KMEM_CACHE_ZALLOC(_cache, _flags) \
	kmem_cache_zalloc((_cache), (_flags))

/**
 * CAM_MEM_KMEM_CACHE_FREE - Return an object to its slab cache.
 *
 * Wraps kmem_cache_free().
 *
 * @_cache : struct kmem_cache * the object was allocated from
 * @_obj   : pointer to the object to free
 */
#define CAM_MEM_KMEM_CACHE_FREE(_cache, _obj) \
	kmem_cache_free((_cache), (_obj))

/**
 * CAM_MEM_KMEM_CACHE_DESTROY - Destroy a slab cache.
 *
 * Wraps kmem_cache_destroy().
 *
 * @_cache : struct kmem_cache * to destroy
 */
#define CAM_MEM_KMEM_CACHE_DESTROY(_cache) \
	kmem_cache_destroy((_cache))

/**
 * struct cam_mem_mgr_request_desc
 *
 * @size    : Size of memory requested for allocation
 * @align   : Alignment of requested memory
 * @smmu_hdl: SMMU handle to identify context bank where memory will be mapped
 * @flags: Flags to indicate cached/uncached property
 */
struct cam_mem_mgr_request_desc {
	uint64_t size;
	uint64_t align;
	int32_t smmu_hdl;
	uint32_t flags;
};

/**
 * struct cam_mem_mgr_memory_desc
 *
 * @kva        : Kernel virtual address of allocated memory
 * @iova       : IOVA of allocated memory
 * @smmu_hdl   : SMMU handle of allocated memory
 * @mem_handle : Mem handle identifying allocated memory
 * @len        : Length of allocated memory
 * @region     : Region to which allocated memory belongs
 */
struct cam_mem_mgr_memory_desc {
	uintptr_t kva;
	uint32_t iova;
	int32_t smmu_hdl;
	uint32_t mem_handle;
	uint64_t len;
	enum cam_smmu_region_id region;
};

/**
 * @brief: Requests a memory buffer
 *
 * @inp:   Information specifying requested buffer properties
 * @out:   Information about allocated buffer
 *
 * @return Status of operation. Negative in case of error. Zero otherwise.
 */
int cam_mem_mgr_request_mem(struct cam_mem_mgr_request_desc *inp,
	struct cam_mem_mgr_memory_desc *out);

/**
 * @brief: Releases a memory buffer
 *
 * @inp:   Information specifying buffer to be released
 *
 * @return Status of operation. Negative in case of error. Zero otherwise.
 */
int cam_mem_mgr_release_mem(struct cam_mem_mgr_memory_desc *inp);

/**
 * @brief: Returns IOVA information about buffer
 *
 * @buf_handle: Handle of the buffer
 * @mmu_handle: SMMU handle where buffer is mapped
 * @iova_ptr  : Pointer to mmu's iova
 * @len_ptr   : Length of the buffer
 * @flags     : Flags the buffer was allocated with
 * @buf_tracker: List of buffers we want to keep ref counts on
 *
 * @return Status of operation. Negative in case of error. Zero otherwise.
 */
int cam_mem_get_io_buf(int32_t buf_handle, int32_t mmu_handle,
	dma_addr_t *iova_ptr, size_t *len_ptr, uint32_t *flags,
	struct list_head *buf_tracker);

/**
 * @brief: This indicates begin of CPU access.
 *         Also returns CPU address information about DMA buffer
 *
 * @buf_handle: Handle for the buffer
 * @vaddr_ptr : pointer to kernel virtual address
 * @len       : Length of the buffer
 *
 * @return Status of operation. Negative in case of error. Zero otherwise.
 */
int cam_mem_get_cpu_buf(int32_t buf_handle, uintptr_t *vaddr_ptr,
	size_t *len);

/**
 * @brief: This indicates end of CPU access
 *
 * @buf_handle: Handle for the buffer
 *
 */
void cam_mem_put_cpu_buf(int32_t buf_handle);

/**
 * @brief: decrements kref reference for a buf handle
 *
 * @buf_handle: Handle for the buffer
 *
 */
void cam_mem_put_kref(int32_t buf_handle);

static inline bool cam_mem_is_secure_buf(int32_t buf_handle)
{
	return CAM_MEM_MGR_IS_SECURE_HDL(buf_handle);
}

/**
 * @brief: Reserves a memory region
 *
 * @inp:  Information specifying requested region properties
 * @region : Region which is to be reserved
 * @out   : Information about reserved region
 *
 * @return Status of operation. Negative in case of error. Zero otherwise.
 */
int cam_mem_mgr_reserve_memory_region(struct cam_mem_mgr_request_desc *inp,
		enum cam_smmu_region_id region,
		struct cam_mem_mgr_memory_desc *out);

/**
 * @brief: Frees a memory region
 *
 * @inp   : Information about region which is to be freed
 *
 * @return Status of operation. Negative in case of error. Zero otherwise.
 */
int cam_mem_mgr_free_memory_region(struct cam_mem_mgr_memory_desc *inp);

/**
 * @brief: Translate fd into dmabuf
 *
 * @inp   : fd for buffer
 *
 * @return dmabuf .
 */
struct dma_buf * cam_mem_mgr_get_dma_buf(int fd);

/**
 * @brief: Initialize the memory trace variables
 */
void cam_mem_trace_init(void);

/**
 * @brief: Allocate a new memory and add it to trace list
 *
 * @size: Size of allocated memory
 * @flag: GFP flags (GFP_KERNEL, GFP_DMA, GFP_ATOMIC etc)
 * @owner: Owner to which allocated memory belongs
 * @line: Exact allocated line of the owner
 *
 * @return None
 */
void *cam_mem_trace_alloc(size_t size, gfp_t flags,
	const char *owner, int line);

/**
 * @brief: Free a allocated memory and delete it from trace list
 *
 * @vaddr_ptr: Kernel virtual address of allocated memory
 *
 * @return None
 */
void cam_mem_trace_free(const void *vaddr_ptr);

/**
 * @brief: Traced version of memdup_user
 */
void *memdup_user_trace(const void __user *src, size_t len,
	const char *owner, int line);

#endif /* _CAM_MEM_MGR_API_H_ */
