/*
 * Virtio network device
 *
 * (C) Copyright 2017 Tuomas Tynkkynen
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <net.h>
#include <virtio.h>

#include <linux/virtio_net.h>

/* Amount of buffers to keep in the RX virtqueue */
#define VIRTNET_NUM_RX_BUFS 4

/*
 * This value comes from the VirtIO spec: 1500 for maximum packet size,
 * 14 for the Ethernet header, 12 for virtio_net_hdr. In total 1526 bytes.
 */
#define VIRTNET_RX_BUF_SIZE 1526

struct virtnet_priv {
	union {
		struct virtqueue *vqs[2];
		struct {
			struct virtqueue *rx_vq;
			struct virtqueue *tx_vq;
		};
	};
};

static int virtnet_start(struct udevice *vndev)
{
	struct virtnet_priv *priv = dev_get_priv(vndev);
	size_t i;

	for (i = 0; i < VIRTNET_NUM_RX_BUFS; i++) {
		void *buf = malloc(VIRTNET_RX_BUF_SIZE);
		struct virtio_sg sg = { buf, VIRTNET_RX_BUF_SIZE };
		struct virtio_sg *sgs[] = { &sg };

		if (!buf)
			goto fail;
		virtqueue_add(priv->rx_vq, sgs, 0, 1);
	}
	virtqueue_kick(priv->rx_vq);

fail: /* FIXME: free memory... */
	return 0;
}

static void virtnet_stop(struct udevice *vdev)
{
	struct virtnet_priv *priv = dev_get_priv(vdev);
	pr_err("%s(%p): Not implemented yet...\n", __func__, priv);
	/* FIXME: free the RX ring buffers... */
}

static int virtnet_send(struct udevice *vdev, void *packet, int length)
{
	struct virtnet_priv *priv = dev_get_priv(vdev);
	struct virtio_net_hdr hdr = {};

	struct virtio_sg hdr_sg = { &hdr, sizeof(hdr) };
	struct virtio_sg data_sg = { packet, length };
	struct virtio_sg *sgs[] = { &hdr_sg, &data_sg };
	int ret;

	ret = virtqueue_add(priv->tx_vq, sgs, 2, 0);
	if (ret)
		return ret;

	virtqueue_kick(priv->tx_vq);
	while (1) {
		if (virtqueue_get_buf(priv->tx_vq, NULL))
			break;
	}

	return 0;
}

static int virtnet_recv(struct udevice *vndev, int flags, uchar **packetp)
{
	struct virtnet_priv *priv = dev_get_priv(vndev);
	unsigned int len;
	void *buf;

	buf = virtqueue_get_buf(priv->rx_vq, &len);
	if (!buf)
		return -EAGAIN;

	*packetp = buf + sizeof(struct virtio_net_hdr);
	return len - sizeof(struct virtio_net_hdr);
}

static int virtnet_free_pkt(struct udevice *vndev, uchar *packet, int length)
{
	struct virtnet_priv *priv = dev_get_priv(vndev);
	void *buf = packet - sizeof(struct virtio_net_hdr);
	struct virtio_sg sg = { buf, VIRTNET_RX_BUF_SIZE };
	struct virtio_sg *sgs[] = { &sg };

	/* Put the buffer back to the rx ring. */
	virtqueue_add(priv->rx_vq, sgs, 0, 1);

	return 0;
}

static int virtnet_probe(struct udevice *vndev)
{
	struct virtnet_priv *priv = dev_get_priv(vndev);
	struct udevice *vdev = vndev->parent;
	int ret;

	pr_err("%s()\n", __func__);
	ret = virtio_find_vqs(vdev, 2, priv->vqs);
	if (ret < 0)
		return ret;

	return 0;
}

static int virtnet_write_hwaddr(struct udevice *vndev)
{
	struct eth_pdata *pdata = dev_get_platdata(vndev);
	struct udevice *vdev = vndev->parent;
	unsigned int i;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENOSYS;

	for (i = 0; i < sizeof(pdata->enetaddr); i++) {
		virtio_cwrite8(vdev,
			       offsetof(struct virtio_net_config, mac) + i,
			       pdata->enetaddr[i]);
	}

	return 0;
}

static int virtnet_read_rom_hwaddr(struct udevice *vndev)
{
	struct eth_pdata *pdata = dev_get_platdata(vndev);
	struct udevice *vdev = vndev->parent;

	pr_err("%s() vndev=%p pdata=%p\n", __func__, vndev, pdata);
	if (!pdata)
		return -ENOSYS;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_MAC)) {
		virtio_cread_bytes(vdev,
				   offsetof(struct virtio_net_config, mac),
				   pdata->enetaddr, sizeof(pdata->enetaddr));
	}
	/* HACK */
	memcpy(pdata->enetaddr, "\x52\x54\x00\x12\x34\x56", 6);

	return 0;
}

static const struct eth_ops virtnet_ops = {
	.start = virtnet_start,
	.send = virtnet_send,
	.recv = virtnet_recv,
	.free_pkt = virtnet_free_pkt,
	.stop = virtnet_stop,
	.write_hwaddr = virtnet_write_hwaddr,
	.read_rom_hwaddr = virtnet_read_rom_hwaddr,
};

U_BOOT_DRIVER(virtnet) = {
	.name	= "virtio_net",
	.id	= UCLASS_ETH,
	.probe	= virtnet_probe,
	.ops	= &virtnet_ops,
	.priv_auto_alloc_size = sizeof(struct virtnet_priv),
	.platdata_auto_alloc_size = sizeof(struct eth_pdata),
};

static const struct virtio_device_id virtnet_supported[] = {
	{ VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID },
	{}
};

U_BOOT_VIRTIO_DEVICE(virtnet, virtnet_supported);
