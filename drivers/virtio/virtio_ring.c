/* Virtio ring implementation.
 *
 *  Copyright 2007 Rusty Russell IBM Corporation
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
 */
#include <common.h>
#include <malloc.h>
#include <virtio.h>
#include <linux/virtio_ring.h>

void dump_virtqueue(struct virtqueue *vq)
{
	unsigned int i;

	printf("Virtqueue %p: index %u, phys addr %p num %u\n", vq, vq->index, vq->vring.desc, vq->vring.num);
	printf("              free_head %u, num_added %u\n", vq->free_head, vq->num_added);
	printf("              last_used_idx %u, avail_flags_shadow %u\n", vq->last_used_idx, vq->avail_flags_shadow);

	printf("    Descriptor dump:\n");
	for (i = 0; i < vq->vring.num; i++) {
		printf("        desc[%-5u] = { 0x%llx, len %u, flags %u, next %u }\n", i,
				vq->vring.desc[i].addr,
				vq->vring.desc[i].len,
				vq->vring.desc[i].flags,
				vq->vring.desc[i].next);
	}

	printf("    Avail ring dump:\n");
	for (i = 0; i < vq->vring.num; i++) {
		printf("        avail[%-5u] = %u\n", i,
				vq->vring.avail->ring[i]);
	}
	printf("    Used ring dump:\n");
	for (i = 0; i < vq->vring.num; i++) {
		printf("        used[%-5u] = { %u, %u }\n", i,
				vq->vring.used->ring[i].id,
				vq->vring.used->ring[i].len);
	}
}

int virtqueue_add(struct virtqueue *vq,
		  struct virtio_sg *sgs[],
		  unsigned int out_sgs,
		  unsigned int in_sgs)
{
	struct vring_desc *desc;
	unsigned int total_sg = out_sgs + in_sgs;
	unsigned int i, n, avail, descs_used, uninitialized_var(prev);
	int head;

	BUG_ON(total_sg == 0);

	head = vq->free_head;

	desc = vq->vring.desc;
	i = head;
	descs_used = total_sg;

	if (vq->num_free < descs_used) {
		debug("Can't add buf len %i - avail = %i\n",
			 descs_used, vq->num_free);
		/* FIXME: for historical reasons, we force a notify here if
		 * there are outgoing parts to the buffer.  Presumably the
		 * host should service the ring ASAP.
		 */
		if (out_sgs)
			virtio_notify(vq->vdev, vq);
		return -ENOSPC;
	}

	for (n = 0; n < out_sgs; n++) {
		struct virtio_sg *sg = sgs[n];
		//dma_addr_t addr = vring_map_one_sg(vq, sg, DMA_TO_DEVICE);

		desc[i].flags = cpu_to_virtio16(vq->vdev, VRING_DESC_F_NEXT);
		desc[i].addr = cpu_to_virtio64(vq->vdev, (u64)(size_t)sg->addr);
		desc[i].len = cpu_to_virtio32(vq->vdev, sg->length);
		//pr_err("%s(): add out sg[%u] %x %llx %u\n", __func__, n, desc[i].flags, desc[i].addr, desc[i].len);
		prev = i;
		i = virtio16_to_cpu(vq->vdev, desc[i].next);
	}
	for (; n < (out_sgs + in_sgs); n++) {
		struct virtio_sg *sg = sgs[n];
		//dma_addr_t addr = vring_map_one_sg(vq, sg, DMA_FROM_DEVICE);

		desc[i].flags = cpu_to_virtio16(vq->vdev, VRING_DESC_F_NEXT | VRING_DESC_F_WRITE);
		desc[i].addr = cpu_to_virtio64(vq->vdev, (u64)(uintptr_t)sg->addr);
		desc[i].len = cpu_to_virtio32(vq->vdev, sg->length);
		//pr_err("%s(): add in sg[%u] %x %llx %u\n", __func__, n, desc[i].flags, desc[i].addr, desc[i].len);
		prev = i;
		i = virtio16_to_cpu(vq->vdev, desc[i].next);
	}
	/* Last one doesn't continue. */
	desc[prev].flags &= cpu_to_virtio16(vq->vdev, ~VRING_DESC_F_NEXT);

	/* We're using some buffers from the free list. */
	vq->num_free -= descs_used;

	/* Update free pointer */
	vq->free_head = i;

	/* Put entry in available array (but don't update avail->idx until they
	 * do sync).
	 */
	avail = vq->avail_idx_shadow & (vq->vring.num - 1);
	vq->vring.avail->ring[avail] = cpu_to_virtio16(vq->vdev, head);

	/* Descriptors and available array need to be set before we expose the
	 * new available array entries.
	 */
	virtio_wmb();
	vq->avail_idx_shadow++;
	vq->vring.avail->idx = cpu_to_virtio16(vq->vdev, vq->avail_idx_shadow);
	vq->num_added++;

	/* This is very unlikely, but theoretically possible.  Kick
	 * just in case.
	 */
	if (unlikely(vq->num_added == (1 << 16) - 1))
		virtqueue_kick(vq);

	/*dump_virtqueue(vq);*/

	return 0;
}

/**
 * virtqueue_kick_prepare - first half of split virtqueue_kick call.
 * @vq: the struct virtqueue
 *
 * Instead of virtqueue_kick(), you can do:
 *	if (virtqueue_kick_prepare(vq))
 *		virtqueue_notify(vq);
 *
 * This is sometimes useful because the virtqueue_kick_prepare() needs
 * to be serialized, but the actual virtqueue_notify() call does not.
 */
bool virtqueue_kick_prepare(struct virtqueue *vq)
{
	u16 new, old;
	bool needs_kick;

	/* We need to expose available array entries before checking avail
	 * event. */
	virtio_mb();

	old = vq->avail_idx_shadow - vq->num_added;
	new = vq->avail_idx_shadow;
	vq->num_added = 0;

	if (vq->event) {
		needs_kick = vring_need_event(virtio16_to_cpu(vq->vdev, vring_avail_event(&vq->vring)),
					      new, old);
	} else {
		needs_kick = !(vq->vring.used->flags & cpu_to_virtio16(vq->vdev, VRING_USED_F_NO_NOTIFY));
	}
	return needs_kick;
}

/**
 * virtqueue_kick - update after add_buf
 * @vq: the struct virtqueue
 *
 * After one or more virtqueue_add_* calls, invoke this to kick
 * the other side.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 *
 * Returns false if kick failed, otherwise true.
 */
void virtqueue_kick(struct virtqueue *vq)
{
	if (virtqueue_kick_prepare(vq))
		virtio_notify(vq->vdev, vq);
}

static void detach_buf(struct virtqueue *vq, unsigned int head)
{
	unsigned int i;
	__virtio16 nextflag = cpu_to_virtio16(vq->vdev, VRING_DESC_F_NEXT);

	/* Put back on free list: unmap first-level descriptors and find end */
	i = head;

	while (vq->vring.desc[i].flags & nextflag) {
		//vring_unmap_one(vq, &vq->vring.desc[i]);
		i = virtio16_to_cpu(vq->vdev, vq->vring.desc[i].next);
		vq->num_free++;
	}

	//vring_unmap_one(vq, &vq->vring.desc[i]);
	vq->vring.desc[i].next = cpu_to_virtio16(vq->vdev, vq->free_head);
	vq->free_head = head;

	/* Plus final descriptor */
	vq->num_free++;
}

static inline bool more_used(const struct virtqueue *vq)
{
	return vq->last_used_idx != virtio16_to_cpu(vq->vdev, vq->vring.used->idx);
}

/**
 * virtqueue_get_buf - get the next used buffer
 * @vq: the struct virtqueue we're talking about.
 * @len: the length written into the buffer
 *
 * If the device wrote data into the buffer, @len will be set to the
 * amount written.  This means you don't need to clear the buffer
 * beforehand to ensure there's no data leakage in the case of short
 * writes.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 *
 * Returns NULL if there are no used buffers, or the memory buffer
 * handed to virtqueue_add_*().
 */
void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len)
{
	unsigned int i;
	u16 last_used;

	/*dump_virtqueue(vq);*/

	if (!more_used(vq)) {
		/* printf("No more buffers in queue\n"); */
		return NULL;
	}

	/* Only get used array entries after they have been exposed by host. */
	virtio_rmb();

	last_used = (vq->last_used_idx & (vq->vring.num - 1));
	i = virtio32_to_cpu(vq->vdev, vq->vring.used->ring[last_used].id);
	*len = virtio32_to_cpu(vq->vdev, vq->vring.used->ring[last_used].len);
	pr_err("%s(): %u %u\n", __func__, i, *len);

	if (unlikely(i >= vq->vring.num)) {
		printf("id %u out of range\n", i);
		return NULL;
	}

	detach_buf(vq, i);
	vq->last_used_idx++;
	/* If we expect an interrupt for the next entry, tell host
	 * by writing event index and flush out the write before
	 * the read in the next get_buf call. */
	if (!(vq->avail_flags_shadow & VRING_AVAIL_F_NO_INTERRUPT))
		virtio_store_mb(&vring_used_event(&vq->vring),
				cpu_to_virtio16(vq->vdev, vq->last_used_idx));

	return (void *)(uintptr_t)virtio64_to_cpu(vq->vdev, vq->vring.desc[i].addr);
}

/**
 * virtqueue_poll - query pending used buffers
 * @vq: the struct virtqueue we're talking about.
 * @last_used_idx: virtqueue state (from call to virtqueue_enable_cb_prepare).
 *
 * Returns "true" if there are pending used buffers in the queue.
 *
 * This does not need to be serialized.
 */
bool virtqueue_poll(struct virtqueue *vq, unsigned int last_used_idx)
{
	virtio_mb();
	return (u16)last_used_idx != virtio16_to_cpu(vq->vdev, vq->vring.used->idx);
}

struct virtqueue *__vring_new_virtqueue(unsigned int index,
					struct vring vring,
					struct udevice *vdev)
{
	unsigned int i;
	struct virtqueue *vq;
	struct virtio_uclass_priv *ucpriv = dev_get_uclass_priv(vdev);

	vq = malloc(sizeof(*vq));
	if (!vq)
		return NULL;

	vq->vring = vring;
	vq->vdev = vdev;
	vq->num_free = vring.num;
	vq->index = index;
	//vq->queue_dma_addr = 0;
	//vq->queue_size_in_bytes = 0;
	vq->last_used_idx = 0;
	vq->avail_flags_shadow = 0;
	vq->avail_idx_shadow = 0;
	vq->num_added = 0;
	list_add_tail(&vq->list, &ucpriv->vqs);

	vq->event = virtio_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX);

	/* No callback?  Tell other side not to bother us. */
	vq->avail_flags_shadow |= VRING_AVAIL_F_NO_INTERRUPT;
	if (!vq->event)
		vq->vring.avail->flags = cpu_to_virtio16(vdev, vq->avail_flags_shadow);

	/* Put everything in free lists. */
	vq->free_head = 0;
	for (i = 0; i < vring.num - 1; i++)
		vq->vring.desc[i].next = cpu_to_virtio16(vdev, i + 1);

	return vq;
}

struct virtqueue *vring_create_virtqueue(
	unsigned int index,
	unsigned int num,
	unsigned int vring_align,
	struct udevice *vdev)
{
	struct virtqueue *vq;
	void *queue = NULL;
	//dma_addr_t dma_addr;
	//size_t queue_size_in_bytes;
	struct vring vring;

	/* We assume num is a power of 2. */
	if (num & (num - 1)) {
		printf("Bad virtqueue length %u\n", num);
		return NULL;
	}

	/* TODO: allocate each queue chunk individually */
	for (; num && vring_size(num, vring_align) > PAGE_SIZE; num /= 2) {
		//queue = vring_alloc_queue(vdev, vring_size(num, vring_align),
					  //&dma_addr);
		queue = memalign(PAGE_SIZE, vring_size(num, vring_align));
		if (queue)
			break;
	}

	if (!num)
		return NULL;

	if (!queue) {
		/* Try to get a single page. You are my only hope! */
		queue = memalign(PAGE_SIZE, vring_size(num, vring_align));
		//queue = vring_alloc_queue(vdev, vring_size(num, vring_align),
					  //&dma_addr, GFP_KERNEL|__GFP_ZERO);
	}
	if (!queue)
		return NULL;

	memset(queue, 0, vring_size(num, vring_align));
	//queue_size_in_bytes = vring_size(num, vring_align);
	vring_init(&vring, num, queue, vring_align);

	vq = __vring_new_virtqueue(index, vring, vdev);
	if (!vq) {
		//vring_free_queue(vdev, queue_size_in_bytes, queue,
				 //dma_addr);
		free(queue);
		return NULL;
	}
	pr_err("%s(): created vring for vq %p with phys=%p num=%u\n", __func__, vq, queue, num);

	//to_vvq(vq)->queue_dma_addr = dma_addr;
	//to_vvq(vq)->queue_size_in_bytes = queue_size_in_bytes;

	return vq;
}

void vring_del_virtqueue(struct virtqueue *vq)
{
	//vring_free_queue(vq->vdev, vq->queue_size_in_bytes,
	//			vq->vring.desc, vq->queue_dma_addr);
	free(vq->vring.desc);
	list_del(&vq->list);
	free(vq);
}

/**
 * virtqueue_get_vring_size - return the size of the virtqueue's vring
 * @vq: the struct virtqueue containing the vring of interest.
 *
 * Returns the size of the vring.  This is mainly used for boasting to
 * userspace.  Unlike other operations, this need not be serialized.
 */
unsigned int virtqueue_get_vring_size(struct virtqueue *vq)
{
	return vq->vring.num;
}

dma_addr_t virtqueue_get_desc_addr(struct virtqueue *vq)
{
	//return vq->queue_dma_addr;
	return (dma_addr_t)vq->vring.desc;
}

dma_addr_t virtqueue_get_avail_addr(struct virtqueue *vq)
{
	//return vq->queue_dma_addr +
	//	((char *)vq->vring.avail - (char *)vq->vring.desc);
	return (dma_addr_t)vq->vring.desc +
		((char *)vq->vring.avail - (char *)vq->vring.desc);
}

dma_addr_t virtqueue_get_used_addr(struct virtqueue *vq)
{
	//return vq->queue_dma_addr +
	//	((char *)vq->vring.used - (char *)vq->vring.desc);
	return (dma_addr_t)vq->vring.desc +
		((char *)vq->vring.used - (char *)vq->vring.desc);
}

const struct vring *virtqueue_get_vring(struct virtqueue *vq)
{
	return &vq->vring;
}
