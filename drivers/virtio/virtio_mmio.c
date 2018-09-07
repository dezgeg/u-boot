/*
 * Virtio memory mapped device driver
 *
 * Copyright 2011-2014, ARM Ltd.
 *
 * This module allows virtio devices to be used over a virtual, memory mapped
 * platform device.
 */

#include <common.h>
#include <dm.h>
#include <virtio.h>
#include <asm/io.h>
#include <linux/virtio_mmio.h>

/* The alignment to use between consumer and producer parts of vring.
 * Currently hardcoded to the page size.
 */
#define VIRTIO_MMIO_VRING_ALIGN		PAGE_SIZE

struct virtio_mmio_priv {
	void __iomem *base;
	unsigned long version;
};

static struct virtio_mmio_priv *to_virtio_mmio_device(struct udevice *vdev)
{
    return (struct virtio_mmio_priv *)dev_get_priv(vdev);
}

/* Configuration interface */

static u64 vm_get_features(struct udevice *vdev)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);
	u64 features;

	writel(1, vm_dev->base + VIRTIO_MMIO_DEVICE_FEATURES_SEL);
	features = readl(vm_dev->base + VIRTIO_MMIO_DEVICE_FEATURES);
	features <<= 32;

	writel(0, vm_dev->base + VIRTIO_MMIO_DEVICE_FEATURES_SEL);
	features |= readl(vm_dev->base + VIRTIO_MMIO_DEVICE_FEATURES);

	return features;
}

static int vm_finalize_features(struct udevice *vdev)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);
	struct virtio_uclass_priv *vdev_priv = to_virtio_uclass_priv(vdev);

	pr_err("%s()\n", __func__);
	/* Give virtio_ring a chance to accept features. */
	// vring_transport_features(vdev); // fixme

	/* Make sure there is are no mixed devices */
	if (vm_dev->version == 2 &&
	    !(vdev_priv->features & VIRTIO_F_VERSION_1)) {
		pr_err("New virtio-mmio devices (version 2) must provide VIRTIO_F_VERSION_1 feature!\n");
		return -EINVAL;
	}

	writel(1, vm_dev->base + VIRTIO_MMIO_DRIVER_FEATURES_SEL);
	writel((u32)(vdev_priv->features >> 32),
	       vm_dev->base + VIRTIO_MMIO_DRIVER_FEATURES);

	writel(0, vm_dev->base + VIRTIO_MMIO_DRIVER_FEATURES_SEL);
	writel((u32)vdev_priv->features,
	       vm_dev->base + VIRTIO_MMIO_DRIVER_FEATURES);

	return 0;
}

static void vm_get(struct udevice *vdev, unsigned int offset,
		   void *buf, unsigned int len)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);
	void __iomem *base = vm_dev->base + VIRTIO_MMIO_CONFIG;
	u8 b;
	__le16 w;
	__le32 l;

	if (vm_dev->version == 1) {
		u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			ptr[i] = readb(base + offset + i);
		return;
	}

	switch (len) {
	case 1:
		b = readb(base + offset);
		memcpy(buf, &b, sizeof(b));
		break;
	case 2:
		w = cpu_to_le16(readw(base + offset));
		memcpy(buf, &w, sizeof(w));
		break;
	case 4:
		l = cpu_to_le32(readl(base + offset));
		memcpy(buf, &l, sizeof(l));
		break;
	case 8:
		l = cpu_to_le32(readl(base + offset));
		memcpy(buf, &l, sizeof(l));
		l = cpu_to_le32(readl(base + offset + sizeof(l)));
		memcpy(buf + sizeof(l), &l, sizeof(l));
		break;
	default:
		BUG();
	}
}

static void vm_set(struct udevice *vdev, unsigned int offset,
		   const void *buf, unsigned int len)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);
	void __iomem *base = vm_dev->base + VIRTIO_MMIO_CONFIG;
	u8 b;
	__le16 w;
	__le32 l;

	if (vm_dev->version == 1) {
		const u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			writeb(ptr[i], base + offset + i);

		return;
	}

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof(b));
		writeb(b, base + offset);
		break;
	case 2:
		memcpy(&w, buf, sizeof(w));
		writew(le16_to_cpu(w), base + offset);
		break;
	case 4:
		memcpy(&l, buf, sizeof(l));
		writel(le32_to_cpu(l), base + offset);
		break;
	case 8:
		memcpy(&l, buf, sizeof(l));
		writel(le32_to_cpu(l), base + offset);
		memcpy(&l, buf + sizeof(l), sizeof(l));
		writel(le32_to_cpu(l), base + offset + sizeof(l));
		break;
	default:
		BUG();
	}
}

static u32 vm_generation(struct udevice *vdev)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);

	if (vm_dev->version == 1)
		return 0;
	else
		return readl(vm_dev->base + VIRTIO_MMIO_CONFIG_GENERATION);
}

static u8 vm_get_status(struct udevice *vdev)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);

	return readl(vm_dev->base + VIRTIO_MMIO_STATUS) & 0xff;
}

static void vm_set_status(struct udevice *vdev, u8 status)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);

	/* We should never be setting status to 0. */
	BUG_ON(status == 0);

	writel(status, vm_dev->base + VIRTIO_MMIO_STATUS);
}

static void vm_reset(struct udevice *vdev)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);

	/* 0 status means a reset. */
	writel(0, vm_dev->base + VIRTIO_MMIO_STATUS);
}

/* Transport interface */

/* the notify function used when creating a virt queue */
static void vm_notify(struct udevice *vdev, struct virtqueue *vq)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);

	pr_err("%s(vdev=%p) base=%p index=%u\n", __func__, vdev, vm_dev->base, vq->index);
	/* We write the queue's selector into the notification register to
	 * signal the other end */
	writel(vq->index, vm_dev->base + VIRTIO_MMIO_QUEUE_NOTIFY);
}

static void vm_del_vq(struct virtqueue *vq)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vq->vdev);
	unsigned int index = vq->index;

	/* Select and deactivate the queue */
	writel(index, vm_dev->base + VIRTIO_MMIO_QUEUE_SEL);
	if (vm_dev->version == 1) {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_READY);
		WARN_ON(readl(vm_dev->base + VIRTIO_MMIO_QUEUE_READY));
	}

	vring_del_virtqueue(vq);
}

static void vm_del_vqs(struct udevice *vdev)
{
	struct virtio_uclass_priv *vdev_priv = to_virtio_uclass_priv(vdev);
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev_priv->vqs, list)
		vm_del_vq(vq);
}

static struct virtqueue *vm_setup_vq(struct udevice *vdev, unsigned int index)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);
	struct virtqueue *vq;
	unsigned int num;
	int err;

	pr_err("%s(vdev=%p) base=%p\n", __func__, vdev, vm_dev->base);
	/* Select the queue we're interested in */
	writel(index, vm_dev->base + VIRTIO_MMIO_QUEUE_SEL);

	pr_err("%s() 2\n", __func__);
	/* Queue shouldn't already be set up. */
	if (readl(vm_dev->base + (vm_dev->version == 1 ?
			VIRTIO_MMIO_QUEUE_PFN : VIRTIO_MMIO_QUEUE_READY))) {
		err = -ENOENT;
		goto error_available;
	}

	pr_err("%s() 2b\n", __func__);
	/* Allocate and fill out our active queue description */
	num = readl(vm_dev->base + VIRTIO_MMIO_QUEUE_NUM_MAX);
	if (num == 0) {
		err = -ENOENT;
		goto error_new_virtqueue;
	}

	/* Create the vring */
	vq = vring_create_virtqueue(index, num, VIRTIO_MMIO_VRING_ALIGN, vdev);
	pr_err("%s() got virtqueue %p\n", __func__, vq);
	if (!vq) {
		err = -ENOMEM;
		goto error_new_virtqueue;
	}

	/* Activate the queue */
	pr_err("%s() 4\n", __func__);
	writel(virtqueue_get_vring_size(vq),
	       vm_dev->base + VIRTIO_MMIO_QUEUE_NUM);
	if (vm_dev->version == 1) {
		writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_QUEUE_ALIGN);
		writel(virtqueue_get_desc_addr(vq) >> PAGE_SHIFT,
		       vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		u64 addr;

		addr = virtqueue_get_desc_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_DESC_LOW);
		writel((u32)(addr >> 32),
		       vm_dev->base + VIRTIO_MMIO_QUEUE_DESC_HIGH);

		addr = virtqueue_get_avail_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_AVAIL_LOW);
		writel((u32)(addr >> 32),
		       vm_dev->base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH);

		addr = virtqueue_get_used_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_USED_LOW);
		writel((u32)(addr >> 32),
		       vm_dev->base + VIRTIO_MMIO_QUEUE_USED_HIGH);

		writel(1, vm_dev->base + VIRTIO_MMIO_QUEUE_READY);
	}

	pr_err("%s() success\n", __func__);
	return vq;

error_new_virtqueue:
	if (vm_dev->version == 1) {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_READY);
		WARN_ON(readl(vm_dev->base + VIRTIO_MMIO_QUEUE_READY));
	}
error_available:
	pr_err("%s() error\n", __func__);
	return ERR_PTR(err);
}

static int vm_find_vqs(struct udevice *vdev, unsigned int nvqs,
		       struct virtqueue *vqs[])
{
	int i;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = vm_setup_vq(vdev, i);
		if (IS_ERR(vqs[i])) {
			vm_del_vqs(vdev);
			return PTR_ERR(vqs[i]);
		}
	}

	return 0;
}

static const struct virtio_config_ops virtio_mmio_config_ops = {
	.get		= vm_get,
	.set		= vm_set,
	.generation	= vm_generation,
	.get_status	= vm_get_status,
	.set_status	= vm_set_status,
	.reset		= vm_reset,
	.find_vqs	= vm_find_vqs,
	.del_vqs	= vm_del_vqs,
	.get_features	= vm_get_features,
	.finalize_features = vm_finalize_features,
	.notify = vm_notify,
};

static int virtio_mmio_probe(struct udevice *vdev)
{
	struct virtio_mmio_priv *vm_dev = to_virtio_mmio_device(vdev);
	unsigned long magic;
	u32 vendor_id, device_id;

	/* Check magic value */
	magic = readl(vm_dev->base + VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
		pr_err("Wrong magic value 0x%08lx!\n", magic);
		return -ENODEV;
	}

	/* Check device version */
	vm_dev->version = readl(vm_dev->base + VIRTIO_MMIO_VERSION);
	if (vm_dev->version < 1 || vm_dev->version > 2) {
		pr_err("Version %ld not supported!\n", vm_dev->version);
		return -ENXIO;
	}

	device_id = readl(vm_dev->base + VIRTIO_MMIO_DEVICE_ID);
	if (device_id == 0) {
		/*
		 * virtio-mmio device with an ID 0 is a (dummy) placeholder
		 * with no function. End probing now with no error reported.
		 */
		return 0;
	}
	vendor_id = readl(vm_dev->base + VIRTIO_MMIO_VENDOR_ID);

	if (vm_dev->version == 1)
		writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);

	return virtio_probe_child_device(vdev, vendor_id, device_id);
}

static int virtio_mmio_ofdata_to_platdata(struct udevice *vdev)
{
	struct virtio_mmio_priv *vm_priv = dev_get_priv(vdev);

	vm_priv->base = (void *)dev_read_addr(vdev);
	if (vm_priv->base == (void *)FDT_ADDR_T_NONE)
		return -EINVAL;

	return 0;
}

static const struct udevice_id virtio_mmio_ids[] = {
	{ .compatible = "virtio,mmio", },
	{},
};

U_BOOT_DRIVER(virtio_mmio_drv) = {
	.name		= "virtio_mmio",
	.id		= UCLASS_VIRTIO,
	.of_match	= virtio_mmio_ids,
	.probe		= virtio_mmio_probe,
	.ops		= &virtio_mmio_config_ops,
	.ofdata_to_platdata = virtio_mmio_ofdata_to_platdata,
	.priv_auto_alloc_size = sizeof(struct virtio_uclass_priv),
};
