/*
 * (C) Copyright 2017 Tuomas Tynkkynen
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <common.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <virtio.h>

#define DEBUG 1

static bool virtio_match_one_id(u32 device, u32 vendor,
				const struct virtio_device_id *id)
{
	return (id->device == VIRTIO_DEV_ANY_ID || id->device == device) &&
		(id->vendor == VIRTIO_DEV_ANY_ID || id->vendor == vendor);
}

int virtio_find_and_bind_driver(struct udevice *parent,
				u32 vendor,
				u32 device,
				struct udevice **devp)
{
	struct virtio_driver_entry *start, *entry;
	const struct virtio_device_id *id;
	int n_ents;
	int ret;

	*devp = NULL;
	pr_debug("%s: Searching for driver\n", __func__);
	start = ll_entry_start(struct virtio_driver_entry,
			       virtio_driver_entry);
	n_ents = ll_entry_count(struct virtio_driver_entry,
				virtio_driver_entry);
	for (entry = start; entry != start + n_ents; entry++) {
		struct udevice *dev;
		const struct driver *drv;

		for (id = entry->match; id->device || id->vendor; id++) {
			pr_err("XXX: try (%u, %u) <-> (%u, %u)\n", device, vendor, id->device, id->vendor);
			if (!virtio_match_one_id(device, vendor, id))
				continue;

			drv = entry->driver;
			/*
			 * We could pass the descriptor to the driver as
			 * platdata (instead of NULL) and allow its bind()
			 * method to return -ENOENT if it doesn't support this
			 * device. That way we could continue the search to
			 * find another driver. For now this doesn't seem
			 * necesssary, so just bind the first match.
			 */
			ret = device_bind(parent, drv, drv->name, NULL, -1,
					  &dev);
			if (ret)
				return ret;
			debug("%s: Match found: %s\n", __func__, drv->name);
			//dev->driver_data = id->driver_info;
			//plat = dev_get_parent_platdata(dev);
			//plat->id = *id;
			*devp = dev;
			return 0;
		}
	}

	return -ENODEV;
}

int virtio_finalize_features(struct udevice *vdev)
{
	int ret = virtio_config_ops(vdev)->finalize_features(vdev);
	unsigned int status;

	if (ret)
		return ret;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return 0;

	virtio_add_status(vdev, VIRTIO_CONFIG_S_FEATURES_OK);
	status = virtio_get_status(vdev);
	if (!(status & VIRTIO_CONFIG_S_FEATURES_OK)) {
		dev_err(vdev, "virtio: device refuses features: %x\n",
			status);
		return -ENODEV;
	}
	return 0;
}

int virtio_probe_child_device(struct udevice *vdev, u32 vendor, u32 device)
{
	struct udevice *child;
	struct virtio_uclass_priv *vdev_priv;
	int ret;

	/* We always start by resetting the device, in case a previous
	 * driver messed it up. This also tests that code path a little.
	 */
	virtio_reset(vdev);

	/* Acknowledge that we've seen the device. */
	virtio_add_status(vdev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	pr_err("%s(%u, %u)\n", __func__, vendor, device);
	ret = virtio_find_and_bind_driver(vdev, vendor, device, &child);
	if (ret)
		goto fail;

	vdev_priv = to_virtio_uclass_priv(vdev);
	vdev_priv->features = 0;
	INIT_LIST_HEAD(&vdev_priv->vqs);

	/* We have a driver! */
	virtio_add_status(vdev, VIRTIO_CONFIG_S_DRIVER);

	/* XXX figure out features here */

	ret = virtio_finalize_features(vdev);
	if (ret)
		goto fail;

	pr_err("%s(%u, %u) registering child device %p to %p\n", __func__, vendor, device, child, vdev);
	device_probe(child);

	virtio_add_status(vdev, VIRTIO_CONFIG_S_DRIVER_OK);

	return 0;

fail:
	virtio_add_status(vdev, VIRTIO_CONFIG_S_FAILED);
	return ret;
}

UCLASS_DRIVER(virtio) = {
	.id		= UCLASS_VIRTIO,
	.name		= "virtio",
	.per_device_auto_alloc_size = sizeof(struct virtio_uclass_priv),
};

UCLASS_DRIVER(virtio_generic_drv) = {
	.id		= UCLASS_VIRTIO_GENERIC,
	.name		= "virtio_generic_drv",
};

static int virtio_curr_dev;

static int do_virtio(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct udevice *vdev;

	if (argc == 2 && !strcmp(argv[1], "scan")) {
		int err;

		for (err = uclass_first_device(UCLASS_VIRTIO, &vdev);
				vdev;
				err = uclass_next_device(&vdev))
			;
		return err ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
	}

	return blk_common_cmd(argc, argv, IF_TYPE_VIRTIO, &virtio_curr_dev);
}

U_BOOT_CMD(
	virtio,	8,	1,	do_virtio,
	"Virtio sub-system",
	"scan - scan virtio devices\n"
	"virtio info - show all available Virtio block devices\n"
	"virtio device [dev] - show or set current Virtio block device\n"
	"virtio part [dev] - print partition table of one or all Virtio block devices\n"
	"virtio read addr blk# cnt - read `cnt' blocks starting at block\n"
	"     `blk#' to memory address `addr'\n"
	"virtio blk write addr blk# cnt - write `cnt' blocks starting at block\n"
	"     `blk#' from memory address `addr'"
);
