/*
 * BCMSDH Function Driver for the native SDIO/MMC driver in the Linux Kernel
 *
 * Copyright (C) 1999-2009, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmsdh_sdmmc_linux.c,v 1.1.2.5.20.8 2009/07/25 03:50:48 Exp $
 */

#include <typedefs.h>
#include <bcmutils.h>
#include <sdio.h>	/* SDIO Specs */
#include <mach/mpp.h>
#include <mach/gpio.h>
#include <bcmsdbus.h>	/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>	/* to get msglevel bit values */

#include <linux/sched.h>	/* request_irq() */

#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif /* defined(CONFIG_HAS_EARLYSUSPEND) */

#if !defined(SDIO_VENDOR_ID_BROADCOM)
#define SDIO_VENDOR_ID_BROADCOM		0x02d0
#endif /* !defined(SDIO_DEVICE_ID_BROADCOM_4325) */
#if !defined(SDIO_DEVICE_ID_BROADCOM_4325)
#define SDIO_DEVICE_ID_BROADCOM_4325	0x0000
#endif /* !defined(SDIO_DEVICE_ID_BROADCOM_4325) */

#include <bcmsdh_sdmmc.h>
#include <dhd_dbg.h>

#if defined(CONFIG_BRCM_GPIO_INTR) && defined(CONFIG_HAS_EARLYSUSPEND)
#include <mach/gpio.h>
#include <linux/irq.h>
#endif	/* #if defined(CONFIG_BRCM_GPIO_INTR) && defined(CONFIG_HAS_EARLYSUSPEND) */

extern void sdioh_sdmmc_devintr_off(sdioh_info_t *sd);
extern void sdioh_sdmmc_devintr_on(sdioh_info_t *sd);

int sdio_function_init(void);
void sdio_function_cleanup(void);

#define DESCRIPTION "bcmsdh_sdmmc Driver"
#define AUTHOR "Broadcom Corporation"

/* module param defaults */
static int clockoverride = 0;

module_param(clockoverride, int, 0644);
MODULE_PARM_DESC(clockoverride, "SDIO card clock override");

PBCMSDH_SDMMC_INSTANCE gInstance;

/* Maximum number of bcmsdh_sdmmc devices supported by driver */
#define BCMSDH_SDMMC_MAX_DEVICES 1

extern int bcmsdh_probe(struct device *dev);
extern int bcmsdh_remove(struct device *dev);
struct device sdmmc_dev;

#if defined(CONFIG_HAS_EARLYSUSPEND)
extern int dhdsdio_bussleep(void *bus, bool sleep);
extern bool dhdsdio_dpc(void *bus);
extern int dhd_os_proto_block(void *pub);
extern int dhd_os_proto_unblock(void * pub);
extern void *dhd_es_get_dhd_pub(void);
extern void *dhd_es_get_dhd_bus_sdh(void);
static int dhd_register_early_suspend(void);
static void dhd_unregister_early_suspend(void);
static void dhd_early_suspend(struct early_suspend *h);
static void dhd_late_resume(struct early_suspend *h);
static struct early_suspend early_suspend_data = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 20,
	.suspend = dhd_early_suspend,
	.resume = dhd_late_resume
};
DECLARE_WAIT_QUEUE_HEAD(bussleep_wake);
typedef struct dhd_early_suspend {
	bool state;
	bool drv_loaded;
	struct dhd_bus_t *bus;
} dhd_early_suspend_t;
dhd_early_suspend_t dhd_early_suspend_ctrl = { 0, 0, 0};
#endif /* #if defined(CONFIG_HAS_EARLYSUSPEND) */

static int bcmsdh_sdmmc_probe(struct sdio_func *func,
                              const struct sdio_device_id *id)
{
	int ret = 0;
	sd_trace(("bcmsdh_sdmmc: %s Enter\n", __FUNCTION__));
	sd_trace(("sdio_bcmsdh: func->class=%x\n", func->class));
	sd_trace(("sdio_vendor: 0x%04x\n", func->vendor));
	sd_trace(("sdio_device: 0x%04x\n", func->device));
	sd_trace(("Function#: 0x%04x\n", func->num));

	if (func->num == 1) {
		/* Keep a copy of F1's 'func' in F0, just in case... */
		gInstance->func[0] = func;
		if(func->device == 0x4) { /* 4318 */
			gInstance->func[2] = NULL;
			sd_trace(("NIC found, calling bcmsdh_probe...\n"));
			bcmsdh_probe(&sdmmc_dev);
		}
	}

	gInstance->func[func->num] = func;

	if (func->num == 2) {
		sd_trace(("F2 found, calling bcmsdh_probe...\n"));
		bcmsdh_probe(&sdmmc_dev);
	}

	return ret;
}

static void bcmsdh_sdmmc_remove(struct sdio_func *func)
{
	sd_trace(("bcmsdh_sdmmc: %s Enter\n", __FUNCTION__));
	sd_info(("sdio_bcmsdh: func->class=%x\n", func->class));
	sd_info(("sdio_vendor: 0x%04x\n", func->vendor));
	sd_info(("sdio_device: 0x%04x\n", func->device));
	sd_info(("Function#: 0x%04x\n", func->num));

	if (func->num == 2) {
		sd_trace(("F2 found, calling bcmsdh_probe...\n"));
		bcmsdh_remove(&sdmmc_dev);
	}
}


/* devices we support, null terminated */
static const struct sdio_device_id bcmsdh_sdmmc_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4325) },
	{ SDIO_DEVICE_CLASS(SDIO_CLASS_NONE)		},
	{ /* end: all zeroes */				},
};

MODULE_DEVICE_TABLE(sdio, bcmsdh_sdmmc_ids);

static struct sdio_driver bcmsdh_sdmmc_driver = {
	.probe		= bcmsdh_sdmmc_probe,
	.remove		= bcmsdh_sdmmc_remove,
	.name		= "bcmsdh_sdmmc",
	.id_table	= bcmsdh_sdmmc_ids,
	};

struct sdos_info {
	sdioh_info_t *sd;
	spinlock_t lock;
};


int
sdioh_sdmmc_osinit(sdioh_info_t *sd)
{
	struct sdos_info *sdos;

	sdos = (struct sdos_info*)MALLOC(sd->osh, sizeof(struct sdos_info));
	sd->sdos_info = (void*)sdos;
	if (sdos == NULL)
		return BCME_NOMEM;

	sdos->sd = sd;
	spin_lock_init(&sdos->lock);
	return BCME_OK;
}

void
sdioh_sdmmc_osfree(sdioh_info_t *sd)
{
	struct sdos_info *sdos;
	ASSERT(sd && sd->sdos_info);

	sdos = (struct sdos_info *)sd->sdos_info;
	MFREE(sd->osh, sdos, sizeof(struct sdos_info));
}

#if defined(CONFIG_HAS_EARLYSUSPEND)
void
dhd_es_set_dhd_bus(void *bus)
{
	dhd_early_suspend_ctrl.bus = bus;
}

void *
dhd_es_get_dhd_bus(void)
{
	return dhd_early_suspend_ctrl.bus;
}

static int
dhd_es_lock_dhd_bus(void)
{
	dhd_os_proto_block(dhd_es_get_dhd_pub());

	return 0;
}

static int
dhd_es_unlock_dhd_bus(void)
{
	dhd_os_proto_unblock(dhd_es_get_dhd_pub());
	return 0;
}

bool
dhd_early_suspend_state(void)
{
	return dhd_early_suspend_ctrl.state;
}

static int dhd_suspend(void)
{
	int bus_state;

	DHD_TRACE(("%s: SUSPEND Enter\n", __FUNCTION__));
	if (NULL != dhd_early_suspend_ctrl.bus) {
		dhd_es_lock_dhd_bus();
		dhd_early_suspend_ctrl.state = TRUE;
		do {
			bus_state = dhdsdio_bussleep(dhd_early_suspend_ctrl.bus, TRUE);
			if (bus_state == BCME_BUSY)
			{
				/* 50ms timeout */
				wait_event_timeout(bussleep_wake, FALSE, HZ/20);
				DHD_TRACE(("%s in loop\n", __FUNCTION__));
			}
		} while (bus_state == BCME_BUSY);
		DHD_TRACE(("%s: SUSPEND Done\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: no bus.. \n", __FUNCTION__));
	}
	return 0;
}
static int dhd_resume(void)
{
	DHD_TRACE(("%s: RESUME Enter\n", __FUNCTION__));
	if (NULL != dhd_early_suspend_ctrl.bus) {
		dhd_early_suspend_ctrl.state = FALSE;
		dhdsdio_dpc(dhd_early_suspend_ctrl.bus);
		dhd_es_unlock_dhd_bus();
		DHD_TRACE(("%s: RESUME Done\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: no bus.. \n", __FUNCTION__));
	}
	return 0;
}
#endif /* #if defined(CONFIG_HAS_EARLYSUSPEND) */


/* Interrupt enable/disable */
SDIOH_API_RC
sdioh_interrupt_set(sdioh_info_t *sd, bool enable)
{
	ulong flags;
	struct sdos_info *sdos;

	sd_trace(("%s: %s\n", __FUNCTION__, enable ? "Enabling" : "Disabling"));

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	if (enable && !(sd->intr_handler && sd->intr_handler_arg)) {
		sd_err(("%s: no handler registered, will not enable\n", __FUNCTION__));
		return SDIOH_API_RC_FAIL;
	}

	/* Ensure atomicity for enable/disable calls */
	spin_lock_irqsave(&sdos->lock, flags);

	sd->client_intr_enabled = enable;
	if (enable) {
		sdioh_sdmmc_devintr_on(sd);
	} else {
		sdioh_sdmmc_devintr_off(sd);
	}

	spin_unlock_irqrestore(&sdos->lock, flags);

	return SDIOH_API_RC_SUCCESS;
}


#ifdef BCMSDH_MODULE
static int __init
bcmsdh_module_init(void)
{
	int error = 0;
	sdio_function_init();
	return error;
}

static void __exit
bcmsdh_module_cleanup(void)
{
	sdio_function_cleanup();
}

module_init(bcmsdh_module_init);
module_exit(bcmsdh_module_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_AUTHOR(AUTHOR);

#endif /* BCMSDH_MODULE */
/*
 * module init
*/
extern int msm_wifi_on();
extern int msm_wifi_off();
extern void (*wifi_status_cb)(int card_present, void *dev_id);
extern void *wifi_status_cb_devid;

int notify_wifi_mmc_detect()
{
        printk("%s: \n", __func__);
        if (wifi_status_cb) {
                wifi_status_cb(0, wifi_status_cb_devid);
        } else
                printk(KERN_WARNING "%s: Nobody to notify\n",__func__);
        return 0;
}

int sdio_function_init(void)
{
	int error = 0;
//	int rc = 0;
	sd_trace(("bcmsdh_sdmmc: %s Enter\n", __FUNCTION__));

#if 0
	rc = gpio_tlmm_config(GPIO_CFG(81, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	if (rc)
		printk(KERN_ERR "%s: gpio_tlmm_config(%#x)=%d\n",
			__func__, 81, rc);

	gpio_set_value(81, 0);
	rc = mpp_config_digital_out(mpp_get(NULL, "mpp4"),
					     MPP_CFG(MPP_DLOGIC_LVL_MSMP,
					     MPP_DLOGIC_OUT_CTRL_LOW));
	if (rc)
		printf("%s: return val: %d \n",
			__func__, rc);

	msleep(100);
	gpio_set_value(81, 1);
	msleep(150);
	rc = mpp_config_digital_out(mpp_get(NULL, "mpp4"),
					     MPP_CFG(MPP_DLOGIC_LVL_MSMP,
					     MPP_DLOGIC_OUT_CTRL_HIGH));
	if (rc)
		printf("%s: return val: %d \n",
			__func__, rc);

	msleep(500);
	printf("------BCM4325 power init sequence! \n");
#endif
	msm_wifi_on();
    msleep(500);
    notify_wifi_mmc_detect();

	gInstance = kzalloc(sizeof(BCMSDH_SDMMC_INSTANCE), GFP_KERNEL);
	if (!gInstance)
		return -ENOMEM;

	error = sdio_register_driver(&bcmsdh_sdmmc_driver);

#if defined(CONFIG_HAS_EARLYSUSPEND)
	if (!error) {
		dhd_register_early_suspend();
		DHD_TRACE(("%s: registered with Android PM\n", __FUNCTION__));
	}
#endif	/* defined(CONFIG_HAS_EARLYSUSPEND) */

	return error;
}

/*
 * module cleanup
*/
extern int bcmsdh_remove(struct device *dev);
void sdio_function_cleanup(void)
{
	sd_trace(("%s Enter\n", __FUNCTION__));

#if defined(CONFIG_HAS_EARLYSUSPEND)
	dhd_unregister_early_suspend();
#endif	/* defined(CONFIG_HAS_EARLYSUSPEND) */
	sdio_unregister_driver(&bcmsdh_sdmmc_driver);
	msm_wifi_off();
//john added for remove mmc
	msleep(200);
    	notify_wifi_mmc_detect();
	
	if (gInstance)
		kfree(gInstance);
}

#if defined(CONFIG_HAS_EARLYSUSPEND)
static void dhd_early_suspend(struct early_suspend *h)
{
//added by john begin
#ifdef DHD_EARLY_SKIP
	return;
#else
//added by john end
	DHD_TRACE(("%s: enter\n", __FUNCTION__));
	dhd_suspend();
#endif
}

static void dhd_late_resume(struct early_suspend *h)
{
//added by john begin
#ifdef DHD_EARLY_SKIP
        return;
#else
//added by john end
	DHD_TRACE(("%s: enter\n", __FUNCTION__));
	dhd_resume();
#endif
}


#if defined(CONFIG_BRCM_GPIO_INTR)

#define GPIO_WLAN_HOST_WAKE 0

struct dhd_wifisleep_info {
	unsigned host_wake;
	unsigned host_wake_irq;
};

static struct dhd_wifisleep_info *dhd_wifi_sleep;

/**
 * Supposed that Early Suspend/Resume is disable
 */
static int
dhd_enable_hwakeup(void)
{
	int ret;

	ret = set_irq_wake(dhd_wifi_sleep->host_wake_irq, 1);

	if (ret < 0) {
		DHD_ERROR(("Couldn't enable WLAN_HOST_WAKE as wakeup interrupt"));
		free_irq(dhd_wifi_sleep->host_wake_irq, NULL);
	}

	return ret;
}

/**
 * Stops the Sleep-Mode Protocol on the Host.
 */
static void
dhd_disable_hwakeup(void)
{

	if (set_irq_wake(dhd_wifi_sleep->host_wake_irq, 0))
		DHD_ERROR(("Couldn't disable hostwake IRQ wakeup mode\n"));
}


/**
 * Schedules a tasklet to run when receiving an interrupt on the
 * <code>HOST_WAKE</code> GPIO pin.
 * @param irq Not used.
 * @param dev_id Not used.
 */
static irqreturn_t
dhd_hostwakeup_isr(int irq, void *dev_id)
{
	int gpio = 0;

	gpio = gpio_get_value(GPIO_WLAN_HOST_WAKE);
	set_irq_type(dhd_wifi_sleep->host_wake_irq, gpio ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH);
	if (!gpio) {
		DHD_INFO(("[WiFi] complete on host-wakeup \n"));

		return IRQ_HANDLED;
	}

	/* schedule a tasklet to handle the change in the host wake line */
	return IRQ_HANDLED;
}

/**
 * Initializes the module.
 * @return On success, 0. On error, -1, and <code>errno</code> is set
 * appropriately.
 */
static int
dhd_register_hwakeup(void)
{
	int ret;

	dhd_wifi_sleep = kzalloc(sizeof(struct dhd_wifisleep_info), GFP_KERNEL);
	if (!dhd_wifi_sleep)
		return -ENOMEM;

	dhd_wifi_sleep->host_wake = GPIO_WLAN_HOST_WAKE;

	ret = gpio_request(dhd_wifi_sleep->host_wake, "wifi_hostwakeup");
	if (ret < 0) {
		DHD_ERROR(("[WiFi] Failed to get gpio_request \n"));
		gpio_free(dhd_wifi_sleep->host_wake);
		return 0;
	}

	ret = gpio_direction_input(dhd_wifi_sleep->host_wake);
	if (ret < 0) {
		DHD_ERROR(("[WiFi] Failed to get direction  \n"));
		return 0;
	}

	dhd_wifi_sleep->host_wake_irq = gpio_to_irq(dhd_wifi_sleep->host_wake);

	if (dhd_wifi_sleep->host_wake_irq  < 0) {
		DHD_ERROR(("[WiFi] Failed to get irq  \n"));
		return 0;
	}

	ret = request_irq(dhd_wifi_sleep->host_wake_irq, dhd_hostwakeup_isr,
		IRQF_DISABLED | IRQF_TRIGGER_HIGH, "wifi_hostwakeup", NULL);
	if (ret) {
		DHD_ERROR(("[WiFi] Failed to get HostWakeUp IRQ \n"));
		free_irq(dhd_wifi_sleep->host_wake_irq, 0);
		return ret;
		/* To do */
	}
	else {
		DHD_INFO(("[WiFi] install HostWakeup IRQ \n"));
	}

	return ret;
}

static void
dhd_unregister_hwakeup(void)
{

	dhd_disable_hwakeup();
	free_irq(dhd_wifi_sleep->host_wake_irq, NULL);
	gpio_free(dhd_wifi_sleep->host_wake);
	kfree(dhd_wifi_sleep);
}
#endif /*  #ifdef CONFIG_BRCM_GPIO_INTR */

static int
dhd_register_early_suspend(void)
{
	register_early_suspend(&early_suspend_data);
	dhd_early_suspend_ctrl.drv_loaded = TRUE;

#if defined(CONFIG_BRCM_GPIO_INTR)
	/* HostWake up */
	dhd_register_hwakeup();
	dhd_enable_hwakeup();
#endif	/* defined(CONFIG_BRCM_GPIO_INTR) */

	return 0;
}

static void
dhd_unregister_early_suspend(void)
{
	if (dhd_early_suspend_ctrl.drv_loaded == FALSE)
		return;
	unregister_early_suspend(&early_suspend_data);

#if defined(CONFIG_BRCM_GPIO_INTR)
	/* HostWake up */
	dhd_unregister_hwakeup();
#endif	/* defined(CONFIG_BRCM_GPIO_INTR) */
}
#endif	/* #if defined(CONFIG_HAS_EARLYSUSPEND) */
