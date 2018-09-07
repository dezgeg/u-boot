#ifndef _VIRTIO_H
#define _VIRTIO_H

#include <common.h>
#include <dm/device.h>
#include <linux/virtio_config.h>
#include <linux/virtio_types.h>
#include <linux/virtio_ring.h>

/* FIXME */
#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#endif

struct virtqueue;

/**
 * virtio_uclass_priv - representation of a device using virtio
 * @vqs: the list of virtqueues for this device.
 * @features: the features supported by both driver and device.
 */
struct virtio_uclass_priv {
	struct list_head vqs;
	u64 features;
};
#define to_virtio_uclass_priv(vdev) (struct virtio_uclass_priv *)dev_get_uclass_priv(vdev)

struct virtio_config_ops {
	void (*get)(struct udevice *vdev, unsigned offset,
		    void *buf, unsigned len);
	void (*set)(struct udevice *vdev, unsigned offset,
		    const void *buf, unsigned len);
	u32 (*generation)(struct udevice *vdev);
	u8 (*get_status)(struct udevice *vdev);
	void (*set_status)(struct udevice *vdev, u8 status);
	void (*reset)(struct udevice *vdev);
	int (*find_vqs)(struct udevice *vdev, unsigned nvqs,
			struct virtqueue *vqs[]);
	void (*del_vqs)(struct udevice *vdev);
	void (*notify)(struct udevice *vdev, struct virtqueue *vq);
	u64 (*get_features)(struct udevice *vdev);
	int (*finalize_features)(struct udevice *vdev);
};

static inline struct virtio_config_ops *virtio_config_ops(struct udevice *vdev)
{
	return (struct virtio_config_ops*)vdev->driver->ops;
}

/**
 * virtio_has_feature - helper to determine if this device has this feature.
 * @vdev: the device
 * @fbit: the feature bit
 */
static inline bool virtio_has_feature(const struct udevice *vdev,
				      unsigned int fbit)
{
	//if (fbit < VIRTIO_TRANSPORT_F_START)
	//	virtio_check_driver_offered_feature(vdev, fbit);

	//return vdev->features & (1ULL << fbit);
	return 0;
}

static inline int virtio_find_vqs(struct udevice* vdev, unsigned nvqs,
				  struct virtqueue** vqs)
{

	return virtio_config_ops(vdev)->find_vqs(vdev, nvqs, vqs);
}

static inline void virtio_reset(struct udevice* vdev)
{
	virtio_config_ops(vdev)->reset(vdev);
}

static inline u8 virtio_get_status(struct udevice* vdev)
{
	return virtio_config_ops(vdev)->get_status(vdev);
}

static inline void virtio_add_status(struct udevice* vdev, u8 status)
{
	u8 old = virtio_get_status(vdev);

	virtio_config_ops(vdev)->set_status(vdev, status | old);
}

static inline void virtio_notify(struct udevice* vdev, struct virtqueue *vq)
{
	virtio_config_ops(vdev)->notify(vdev, vq);
}


/* From Linux include/linux/virtio_ring.h */
static inline void virtio_mb(void)
{
	//mb();
}

static inline void virtio_rmb(void)
{
	//rmb();
}

static inline void virtio_wmb(void)
{
	//wmb();
}

static inline void virtio_store_mb(__virtio16 *p, __virtio16 v)
{
	WRITE_ONCE(*p, v);
	//mb();
}

/* From Linux include/linux/virtio_byteorder.h */
static inline bool virtio_legacy_is_little_endian(void)
{
#ifdef __LITTLE_ENDIAN
	return true;
#else
	return false;
#endif
}

static inline u16 __virtio16_to_cpu(bool little_endian, __virtio16 val)
{
	if (little_endian)
		return le16_to_cpu((__force __le16)val);
	else
		return be16_to_cpu((__force __be16)val);
}

static inline __virtio16 __cpu_to_virtio16(bool little_endian, u16 val)
{
	if (little_endian)
		return (__force __virtio16)cpu_to_le16(val);
	else
		return (__force __virtio16)cpu_to_be16(val);
}

static inline u32 __virtio32_to_cpu(bool little_endian, __virtio32 val)
{
	if (little_endian)
		return le32_to_cpu((__force __le32)val);
	else
		return be32_to_cpu((__force __be32)val);
}

static inline __virtio32 __cpu_to_virtio32(bool little_endian, u32 val)
{
	if (little_endian)
		return (__force __virtio32)cpu_to_le32(val);
	else
		return (__force __virtio32)cpu_to_be32(val);
}

static inline u64 __virtio64_to_cpu(bool little_endian, __virtio64 val)
{
	if (little_endian)
		return le64_to_cpu((__force __le64)val);
	else
		return be64_to_cpu((__force __be64)val);
}

static inline __virtio64 __cpu_to_virtio64(bool little_endian, u64 val)
{
	if (little_endian)
		return (__force __virtio64)cpu_to_le64(val);
	else
		return (__force __virtio64)cpu_to_be64(val);
}

/* From Linux include/linux/virtio_config.h */
static inline bool virtio_is_little_endian(struct udevice *vdev)
{
	return virtio_has_feature(vdev, VIRTIO_F_VERSION_1) ||
		virtio_legacy_is_little_endian();
}

/* Memory accessors */
static inline u16 virtio16_to_cpu(struct udevice *vdev, __virtio16 val)
{
	return __virtio16_to_cpu(virtio_is_little_endian(vdev), val);
}

static inline __virtio16 cpu_to_virtio16(struct udevice *vdev, u16 val)
{
	return __cpu_to_virtio16(virtio_is_little_endian(vdev), val);
}

static inline u32 virtio32_to_cpu(struct udevice *vdev, __virtio32 val)
{
	return __virtio32_to_cpu(virtio_is_little_endian(vdev), val);
}

static inline __virtio32 cpu_to_virtio32(struct udevice *vdev, u32 val)
{
	return __cpu_to_virtio32(virtio_is_little_endian(vdev), val);
}

static inline u64 virtio64_to_cpu(struct udevice *vdev, __virtio64 val)
{
	return __virtio64_to_cpu(virtio_is_little_endian(vdev), val);
}

static inline __virtio64 cpu_to_virtio64(struct udevice *vdev, u64 val)
{
	return __cpu_to_virtio64(virtio_is_little_endian(vdev), val);
}

/* Config space accessors. */
#define virtio_cread(vdev, structname, member, ptr)			\
	do {								\
		/* Must match the member's type, and be integer */	\
		if (!typecheck(typeof((((structname*)0)->member)), *(ptr))) \
			(*ptr) = 1;					\
									\
		switch (sizeof(*ptr)) {					\
		case 1:							\
			*(ptr) = virtio_cread8(vdev,			\
					       offsetof(structname, member)); \
			break;						\
		case 2:							\
			*(ptr) = virtio_cread16(vdev,			\
						offsetof(structname, member)); \
			break;						\
		case 4:							\
			*(ptr) = virtio_cread32(vdev,			\
						offsetof(structname, member)); \
			break;						\
		case 8:							\
			*(ptr) = virtio_cread64(vdev,			\
						offsetof(structname, member)); \
			break;						\
		default:						\
			BUG();						\
		}							\
	} while(0)

/* Read @count fields, @bytes each. */
static inline void __virtio_cread_many(struct udevice *vdev,
				       unsigned int offset,
				       void *buf, size_t count, size_t bytes)
{
	u32 old, gen = virtio_config_ops(vdev)->generation ?
		virtio_config_ops(vdev)->generation(vdev) : 0;
	int i;

	do {
		old = gen;

		for (i = 0; i < count; i++)
			virtio_config_ops(vdev)->get(vdev, offset + bytes * i,
					  buf + i * bytes, bytes);

		gen = virtio_config_ops(vdev)->generation ?
			virtio_config_ops(vdev)->generation(vdev) : 0;
	} while (gen != old);
}
static inline void virtio_cread_bytes(struct udevice *vdev,
				      unsigned int offset,
				      void *buf, size_t len)
{
	__virtio_cread_many(vdev, offset, buf, len, 1);
}

static inline u8 virtio_cread8(struct udevice *vdev, unsigned int offset)
{
	u8 ret;
	virtio_config_ops(vdev)->get(vdev, offset, &ret, sizeof(ret));
	return ret;
}

static inline void virtio_cwrite8(struct udevice *vdev,
				  unsigned int offset, u8 val)
{
	virtio_config_ops(vdev)->set(vdev, offset, &val, sizeof(val));
}

static inline u16 virtio_cread16(struct udevice *vdev,
				 unsigned int offset)
{
	u16 ret;
	virtio_config_ops(vdev)->get(vdev, offset, &ret, sizeof(ret));
	return virtio16_to_cpu(vdev, (__force __virtio16)ret);
}

static inline void virtio_cwrite16(struct udevice *vdev,
				   unsigned int offset, u16 val)
{
	val = (__force u16)cpu_to_virtio16(vdev, val);
	virtio_config_ops(vdev)->set(vdev, offset, &val, sizeof(val));
}

static inline u32 virtio_cread32(struct udevice *vdev,
				 unsigned int offset)
{
	u32 ret;
	virtio_config_ops(vdev)->get(vdev, offset, &ret, sizeof(ret));
	return virtio32_to_cpu(vdev, (__force __virtio32)ret);
}

static inline void virtio_cwrite32(struct udevice *vdev,
				   unsigned int offset, u32 val)
{
	val = (__force u32)cpu_to_virtio32(vdev, val);
	virtio_config_ops(vdev)->set(vdev, offset, &val, sizeof(val));
}

static inline u64 virtio_cread64(struct udevice *vdev,
				 unsigned int offset)
{
	u64 ret;
	__virtio_cread_many(vdev, offset, &ret, 1, sizeof(ret));
	return virtio64_to_cpu(vdev, (__force __virtio64)ret);
}

static inline void virtio_cwrite64(struct udevice *vdev,
				   unsigned int offset, u64 val)
{
	val = (__force u64)cpu_to_virtio64(vdev, val);
	virtio_config_ops(vdev)->set(vdev, offset, &val, sizeof(val));
}

struct virtio_sg {
	void* addr;
	size_t length;
};

/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @list: the chain of virtqueues for this device
 * @callback: the function to call when buffers are consumed (can be NULL).
 * @name: the name of this virtqueue (mainly for debugging)
 * @vdev: the virtio device this queue was created for.
 * @priv: a pointer for the virtqueue implementation to use.
 * @index: the zero-based ordinal number for this queue.
 * @num_free: number of elements we expect to be able to fit.
 */
struct virtqueue {
	struct list_head list;
	struct udevice *vdev;
	unsigned int index;
	unsigned int num_free;

	/* Actual memory layout for this queue */
	struct vring vring;

	/* Host publishes avail event idx */
	bool event;

	/* Head of free buffer list. */
	unsigned int free_head;
	/* Number we've added since last sync. */
	unsigned int num_added;

	/* Last used index we've seen. */
	u16 last_used_idx;

	/* Last written value to avail->flags */
	u16 avail_flags_shadow;

	/* Last written value to avail->idx in guest byte order */
	u16 avail_idx_shadow;
};

int virtqueue_add(struct virtqueue *vq,
		  struct virtio_sg *sgs[],
		  unsigned int out_sgs,
		  unsigned int in_sgs);

void virtqueue_kick(struct virtqueue *vq);

bool virtqueue_kick_prepare(struct virtqueue *vq);

bool virtqueue_notify(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

bool virtqueue_poll(struct virtqueue *vq, unsigned);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);

unsigned int virtqueue_get_vring_size(struct virtqueue *vq);

struct virtqueue *vring_create_virtqueue(
	unsigned int index,
	unsigned int num,
	unsigned int vring_align,
	struct udevice *vdev);

void vring_del_virtqueue(struct virtqueue *vq);

const struct vring *virtqueue_get_vring(struct virtqueue *vq);
dma_addr_t virtqueue_get_desc_addr(struct virtqueue *vq);
dma_addr_t virtqueue_get_avail_addr(struct virtqueue *vq);
dma_addr_t virtqueue_get_used_addr(struct virtqueue *vq);

int virtio_probe_child_device(struct udevice *parent, u32 vendor, u32 device);

struct virtio_device_id {
	__u32 device;
	__u32 vendor;
};
#define VIRTIO_DEV_ANY_ID	0xffffffff

struct virtio_driver_entry {
	struct driver *driver;
	const struct virtio_device_id *match;
};

#define U_BOOT_VIRTIO_DEVICE(__name, __match) \
	ll_entry_declare(struct virtio_driver_entry, __name, virtio_driver_entry) = {\
		.driver = llsym(struct driver, __name, driver), \
		.match = __match, \
		}

#endif /* _VIRTIO_H */
