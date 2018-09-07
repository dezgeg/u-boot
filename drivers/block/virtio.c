/*
 * Copyright (C) 2017 Tuomas Tynkkynen
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <blk.h>
#include <dm.h>
#include <part.h>
#include <malloc.h>
#include <virtio.h>
#include <linux/virtio_blk.h>
#include <dm/device-internal.h>

struct virtblk_priv {
	struct virtqueue *vq;
};

static int virtblk_do_req(struct udevice *blkdev, u32 type, u64 sector,
			  void *buffer, lbaint_t blkcnt)
{
	struct udevice *vbdev = blkdev->parent;
	struct udevice *vdev = vbdev->parent;
	struct virtblk_priv *vbpriv = dev_get_priv(vbdev);
	unsigned int num_out = 0, num_in = 0;
	struct virtio_sg *sgs[3];
	u8 status;
	int ret;

	struct virtio_blk_outhdr out_hdr = {
		.type = cpu_to_virtio32(vdev, type),
		.sector = cpu_to_virtio64(vdev, sector),
	};
	struct virtio_sg hdr_sg = { &out_hdr, sizeof(out_hdr) };
	struct virtio_sg data_sg = { buffer, blkcnt * 512 };
	struct virtio_sg status_sg = { &status, sizeof(status) };

	sgs[num_out++] = &hdr_sg;

	if (type & VIRTIO_BLK_T_OUT)
		sgs[num_out++] = &data_sg;
	else
		sgs[num_out + num_in++] = &data_sg;

	sgs[num_out + num_in++] = &status_sg;

	ret = virtqueue_add(vbpriv->vq, sgs, num_out, num_in);
	if (ret)
		return ret;

	virtqueue_kick(vbpriv->vq);

	while (!virtqueue_get_buf(vbpriv->vq, NULL))
	    ;

	return status == VIRTIO_BLK_S_OK ? blkcnt : -1;
}

static unsigned long virtblk_block_read(struct udevice *blkdev,
					unsigned long start, lbaint_t blkcnt,
					void *buffer)
{
	return virtblk_do_req(blkdev, VIRTIO_BLK_T_IN, start, buffer, blkcnt);
}

static unsigned long virtblk_block_write(struct udevice *blkdev,
					 unsigned long start, lbaint_t blkcnt,
					 const void *buffer)
{
	return virtblk_do_req(blkdev, VIRTIO_BLK_T_OUT, start,
			      (void *)buffer, blkcnt);
}

static const struct blk_ops virtblk_blk_ops = {
	.read	= virtblk_block_read,
	.write	= virtblk_block_write,
};

static int virtblk_probe(struct udevice *vbdev)
{
	struct udevice *vdev = vbdev->parent;
	struct virtblk_priv *vbpriv = dev_get_priv(vbdev);
	struct blk_desc *desc;
	struct udevice *blkdev;
	int ret;
	u64 cap;

	virtio_cread(vdev, struct virtio_blk_config, capacity, &cap);

	/* FIXME: support non 512-byte sector devices? */
	ret = blk_create_devicef(vbdev, "virtblk", "blk", IF_TYPE_VIRTIO, -1,
				 512, cap, &blkdev);
	if (ret < 0)
		return ret;

	ret = virtio_find_vqs(vdev, 1, &vbpriv->vq);
	if (ret < 0)
		goto out_unbind;

	desc = dev_get_uclass_platdata(blkdev);
	part_init(desc);

	return 0;

out_unbind:
	device_unbind(blkdev);
	return ret;
}

U_BOOT_DRIVER(virtblk_blk) = {
	.name = "virtblk",
	.id = UCLASS_BLK,
	.ops = &virtblk_blk_ops,
};

U_BOOT_DRIVER(virtblk) = {
	.name = "virtio_blk",
	.id = UCLASS_VIRTIO_GENERIC,
	.probe = virtblk_probe,
	.priv_auto_alloc_size = sizeof(struct virtblk_priv),
};

static const struct virtio_device_id virtblk_supported[] = {
	{ VIRTIO_ID_BLOCK, VIRTIO_DEV_ANY_ID },
	{}
};

U_BOOT_VIRTIO_DEVICE(virtblk, virtblk_supported);
