
#include <linux/types.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/clkdev.h>

#include <mach/clkdev.h>
#include <mach/hardware.h>
#include <mach/memory.h>
#include <mach/io_map.h>

#include <plat/bsp.h>
#include <plat/mpcore.h>
#include <plat/plat-bcm5301x.h>

#ifdef CONFIG_MTD_PARTITIONS
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/romfs_fs.h>
#include <linux/cramfs_fs.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
#include <linux/squashfs_fs.h>
#else
/* #include <magic.h> */
#endif
#endif

#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmnvram.h>
#include <bcmendian.h>
#include <hndsoc.h>
#include <siutils.h>
#include <hndcpu.h>
#include <hndpci.h>
#include <pcicfg.h>
#include <bcmdevs.h>
#include <trxhdr.h>
#ifdef HNDCTF
#include <ctf/hndctf.h>
#endif /* HNDCTF */
#include <hndsflash.h>
#ifdef CONFIG_MTD_NFLASH
#include <hndnand.h>
#endif

extern char __initdata saved_root_name[];

/* Global SB handle */
si_t *bcm947xx_sih = NULL;
spinlock_t bcm947xx_sih_lock = SPIN_LOCK_UNLOCKED;
EXPORT_SYMBOL(bcm947xx_sih);
EXPORT_SYMBOL(bcm947xx_sih_lock);

/* Convenience */
#define sih bcm947xx_sih
#define sih_lock bcm947xx_sih_lock

#define WATCHDOG_MIN    3000    /* milliseconds */
extern int panic_timeout;
extern int panic_on_oops;
static int watchdog = 0;

#ifdef HNDCTF
ctf_t *kcih = NULL;
EXPORT_SYMBOL(kcih);
ctf_attach_t ctf_attach_fn = NULL;
EXPORT_SYMBOL(ctf_attach_fn);
#endif /* HNDCTF */

/* To store real PHYS_OFFSET value */
unsigned int ddr_phys_offset_va = -1;
EXPORT_SYMBOL(ddr_phys_offset_va);

/* For NS-Ax ACP only */
unsigned int ns_acp_win_size = SZ_256M;
EXPORT_SYMBOL(ns_acp_win_size);

struct dummy_super_block {
	u32	s_magic ;
};

/* This is the main reference clock 25MHz from external crystal */
static struct clk clk_ref = {
	.name = "Refclk",
	.rate = 25 * 1000000,	/* run-time override */
	.fixed = 1,
	.type	= CLK_XTAL,
};


static struct clk_lookup board_clk_lookups[] = {
	{
	.con_id		= "refclk",
	.clk		= &clk_ref,
	}
};

static struct mtd_partition bcm947xx_parts[] =
{
	{
		.name = "boot",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
	{
		.name = "linux",
		.size = 0,
		.offset = 0
	},
	{
		.name = "rootfs",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
	/* reserve sectors for multi-language implementation */
	/* Foxconn add start, Tony W.Y. Wang, 01/12/2010 */
	{ 
		.name = "ML1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML2", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML3", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML4", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML5", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML6", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML7", 
		.offset = 0, 
		.size = 0, 
	},
	/* Foxconn add end, Tony W.Y. Wang, 01/12/2010 */
	/* Foxconn added start pling 06/30/2006 */
	{ 
		.name = "T_Meter1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "T_Meter2", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "POT", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "board_data", 
		.offset = 0, 
		.size = 0, 
	},
	/* Foxconn added end pling 06/30/2006 */
	{
		.name = "nvram",
		.size = 0,
		.offset = 0
	},
	{
		.name = 0,
		.size = 0,
		.offset = 0
	}
};

extern int _memsize;

void __init board_map_io(void)
{
early_printk("board_map_io\n");
	/* Install clock sources into the lookup table */
	clkdev_add_table(board_clk_lookups, 
			ARRAY_SIZE(board_clk_lookups));

	/* Map SoC specific I/O */
	soc_map_io( &clk_ref );
}

#if defined(CONFIG_SPI_SPIDEV) && defined(CONFIG_SPI_BCM5301X)

#include <linux/spi/spi.h>

static struct spi_board_info spidev_board_info[] __initdata = {
	{
		.modalias	= "spidev",
		.mode		= SPI_MODE_0,
		.max_speed_hz	= (1 << 20 ), /* 1Mhz */
		.bus_num	= 1,
	},
};

static void __init board_init_spi(void)
{
	spi_register_board_info(spidev_board_info, ARRAY_SIZE(spidev_board_info));
}
#else
inline void board_init_spi(void)
{
}
#endif


void __init board_init_irq(void)
{
early_printk("board_init_irq\n");
	soc_init_irq();
	
	/* serial_setup(sih); */
}

void board_init_timer(void)
{
early_printk("board_init_timer\n");
	soc_init_timer();
}

static int __init rootfs_mtdblock(void)
{
	int bootdev;
	int knldev;
	int block = 0;
#ifdef CONFIG_FAILSAFE_UPGRADE
	char *img_boot = nvram_get(BOOTPARTITION);
#endif

	bootdev = soc_boot_dev((void *)sih);
	knldev = soc_knl_dev((void *)sih);

	/* NANDBOOT */
	if (bootdev == SOC_BOOTDEV_NANDFLASH &&
	    knldev == SOC_KNLDEV_NANDFLASH) {
#ifdef CONFIG_FAILSAFE_UPGRADE
		if (img_boot && simple_strtol(img_boot, NULL, 10))
			return 5;
		else
			return 3;
#else
		/*Foxconn modify by Hank 10/24/2012*/
		/*change linux partition when using nandflash boot up*/

#if defined(R6200v2)    
        return 3; 
#elif defined(R7000)
		return 3; 
#else
		return 15; 
#endif
#endif
	}

	/* SFLASH/PFLASH only */
	if (bootdev != SOC_BOOTDEV_NANDFLASH &&
	    knldev != SOC_KNLDEV_NANDFLASH) {
#ifdef CONFIG_FAILSAFE_UPGRADE
		if (img_boot && simple_strtol(img_boot, NULL, 10))
			return 4;
		else
			return 2;
#else
		return 2;
#endif
	}

#ifdef BCMCONFMTD
	block++;
#endif
#ifdef CONFIG_FAILSAFE_UPGRADE
	if (img_boot && simple_strtol(img_boot, NULL, 10))
		block += 2;
#endif
	/* Boot from norflash and kernel in nandflash */
	
	return (sizeof(bcm947xx_parts)/sizeof(struct mtd_partition));	/* Bob modified */
}

static void __init brcm_setup(void)
{
	/* Get global SB handle */
	sih = si_kattach(SI_OSH);

	if (ACP_WAR_ENAB() && BCM4707_CHIP(CHIPID(sih->chip))) {
		if (sih->chippkg == BCM4708_PKG_ID)
			ns_acp_win_size = SZ_128M;
		else if (sih->chippkg == BCM4707_PKG_ID)
			ns_acp_win_size = SZ_32M;
		else
			ns_acp_win_size = SZ_256M;
	} else if (BCM4707_CHIP(CHIPID(sih->chip)) &&
		(CHIPREV(sih->chiprev) == 4 || CHIPREV(sih->chiprev) == 6)) {
		/* Chiprev 4 for NS-B0 and chiprev 6 for NS-B1 */
		ns_acp_win_size = SZ_1G;
	}

	if (strncmp(boot_command_line, "root=/dev/mtdblock", strlen("root=/dev/mtdblock")) == 0)
		sprintf(saved_root_name, "/dev/mtdblock%d", rootfs_mtdblock());

	/* Set watchdog interval in ms */
        watchdog = simple_strtoul(nvram_safe_get("watchdog"), NULL, 0);

	/* Ensure at least WATCHDOG_MIN */
	if ((watchdog > 0) && (watchdog < WATCHDOG_MIN))
		watchdog = WATCHDOG_MIN;

	/* Set panic timeout in seconds */
	panic_timeout = watchdog / 1000;
	panic_on_oops = watchdog / 1000;
}

void soc_watchdog(void)
{
	if (watchdog > 0)
		si_watchdog_ms(sih, watchdog);
}

void __init board_init(void)
{
early_printk("board_init\n");
	brcm_setup();
	/*
	 * Add common platform devices that do not have board dependent HW
	 * configurations
	 */
	soc_add_devices();

	board_init_spi();

	printk(KERN_NOTICE "ACP (Accelerator Coherence Port) %s\n",
		(ACP_WAR_ENAB() || arch_is_coherent()) ? "enabled" : "disabled");

	return;
}

static void __init board_fixup(
	struct machine_desc *desc,
	struct tag *t,
	char **cmdline,
	struct meminfo *mi
	)
{
	u32 mem_size, lo_size ;
        early_printk("board_fixup\n" );

	/* Fuxup reference clock rate */
	if (desc->nr == MACH_TYPE_BRCM_NS_QT )
		clk_ref.rate = 17594;	/* Emulator ref clock rate */


#if defined(BCM_GMAC3)
	/* In ATLAS builds, cap the total memory to 256M (for both Ax and Bx). */
	if (_memsize > SZ_256M) {
		_memsize = SZ_256M;
		early_printk("ATLAS board_fixup: cap memory to 256M\n");
	}
#endif /* BCM_GMAC3 */
	mem_size = _memsize;

	early_printk("board_fixup: mem=%uMiB\n", mem_size >> 20);

	/* for NS-B0-ACP */
	if (ACP_WAR_ENAB() || arch_is_coherent()) {
		mi->bank[0].start = PHYS_OFFSET;
		mi->bank[0].size = mem_size;
		mi->nr_banks++;
		return;
	}

	lo_size = min(mem_size, DRAM_MEMORY_REGION_SIZE);

	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = lo_size;
	mi->nr_banks++;

	if (lo_size == mem_size)
		return;

	mi->bank[1].start = PHYS_OFFSET2;
	mi->bank[1].size = mem_size - lo_size;
	mi->nr_banks++;
}

#ifdef CONFIG_ZONE_DMA
/*
 * Adjust the zones if there are restrictions for DMA access.
 */
void __init bcm47xx_adjust_zones(unsigned long *size, unsigned long *hole)
{
	unsigned long dma_size = SZ_128M >> PAGE_SHIFT;

	if (size[0] <= dma_size)
		return;

	if (ACP_WAR_ENAB() || arch_is_coherent())
		return;

	size[ZONE_NORMAL] = size[0] - dma_size;
	size[ZONE_DMA] = dma_size;
	hole[ZONE_NORMAL] = hole[0];
	hole[ZONE_DMA] = 0;
}
#endif



static struct sys_timer board_timer = {
   .init = board_init_timer,
};

#if (( (IO_BASE_VA >>18) & 0xfffc) != 0x3c40)
#error IO_BASE_VA 
#endif

MACHINE_START(BRCM_NS, "Northstar Prototype")
   .phys_io = 					/* UART I/O mapping */
	IO_BASE_PA,
   .io_pg_offst = 				/* for early debug */
	(IO_BASE_VA >>18) & 0xfffc,
   .fixup = board_fixup,			/* Opt. early setup_arch() */
   .map_io = board_map_io,			/* Opt. from setup_arch() */
   .init_irq = board_init_irq,			/* main.c after setup_arch() */
   .timer  = &board_timer,			/* main.c after IRQs */
   .init_machine = board_init,			/* Late archinitcall */
   .boot_params = CONFIG_BOARD_PARAMS_PHYS,
MACHINE_END

#ifdef	CONFIG_MACH_BRCM_NS_QT
MACHINE_START(BRCM_NS_QT, "Northstar Emulation Model")
   .phys_io = 					/* UART I/O mapping */
	IO_BASE_PA,
   .io_pg_offst = 				/* for early debug */
	(IO_BASE_VA >>18) & 0xfffc,
   .fixup = board_fixup,			/* Opt. early setup_arch() */
   .map_io = board_map_io,			/* Opt. from setup_arch() */
   .init_irq = board_init_irq,			/* main.c after setup_arch() */
   .timer  = &board_timer,			/* main.c after IRQs */
   .init_machine = board_init,			/* Late archinitcall */
   .boot_params = CONFIG_BOARD_PARAMS_PHYS,
MACHINE_END
#endif

void arch_reset(char mode, const char *cmd)
{
#ifdef CONFIG_OUTER_CACHE_SYNC
	outer_cache.sync = NULL;
#endif
	hnd_cpu_reset(sih);
}

#ifdef CONFIG_MTD_PARTITIONS

static spinlock_t *mtd_lock = NULL;

spinlock_t *partitions_lock_init(void)
{
	if (!mtd_lock) {
		mtd_lock = (spinlock_t *)kzalloc(sizeof(spinlock_t), GFP_KERNEL);
		if (!mtd_lock)
			return NULL;

		spin_lock_init( mtd_lock );
	}
	return mtd_lock;
}
EXPORT_SYMBOL(partitions_lock_init);

static struct nand_hw_control *nand_hwcontrol = NULL;
struct nand_hw_control *nand_hwcontrol_lock_init(void)
{
	if (!nand_hwcontrol) {
		nand_hwcontrol = (struct nand_hw_control *)kzalloc(sizeof(struct nand_hw_control), GFP_KERNEL);
		if (!nand_hwcontrol)
			return NULL;

		spin_lock_init(&nand_hwcontrol->lock);
		init_waitqueue_head(&nand_hwcontrol->wq);
	}
	return nand_hwcontrol;
}
EXPORT_SYMBOL(nand_hwcontrol_lock_init);

/* Find out prom size */
static uint32 boot_partition_size(uint32 flash_phys) {
	uint32 bootsz, *bisz;

	/* Default is 256K boot partition */
	bootsz = 256 * 1024;

	/* Do we have a self-describing binary image? */
	bisz = (uint32 *)(flash_phys + BISZ_OFFSET);
	if (bisz[BISZ_MAGIC_IDX] == BISZ_MAGIC) {
		int isz = bisz[BISZ_DATAEND_IDX] - bisz[BISZ_TXTST_IDX];

		if (isz > (1024 * 1024))
			bootsz = 2048 * 1024;
		else if (isz > (512 * 1024))
			bootsz = 1024 * 1024;
		else if (isz > (256 * 1024))
			bootsz = 512 * 1024;
		else if (isz <= (128 * 1024))
			bootsz = 128 * 1024;
	}
	return bootsz;
}

#if defined(BCMCONFMTD)
#define MTD_PARTS 1
#else
#define MTD_PARTS 0
#endif
#if defined(CONFIG_FAILSAFE_UPGRADE)
#define FAILSAFE_PARTS 2
#else
#define FAILSAFE_PARTS 0
#endif
#if defined(CONFIG_CRASHLOG)
#define CRASHLOG_PARTS 1
#else
#define CRASHLOG_PARTS 0
#endif
/* boot;nvram;kernel;rootfs;empty */
#define FLASH_PARTS_NUM	(15+MTD_PARTS+FAILSAFE_PARTS+CRASHLOG_PARTS)

//static struct mtd_partition bcm947xx_flash_parts[FLASH_PARTS_NUM] = {{0}};

static uint lookup_flash_rootfs_offset(struct mtd_info *mtd, int *trx_off, size_t size, 
                                       uint32 *trx_size)
{
	struct romfs_super_block *romfsb;
	struct cramfs_super *cramfsb;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	struct squashfs_super_block *squashfsb;
#else
	struct dummy_super_block *squashfsb;
#endif
	struct trx_header *trx;
	unsigned char buf[512];
	int off;
	size_t len;

	romfsb = (struct romfs_super_block *) buf;
	cramfsb = (struct cramfs_super *) buf;
	squashfsb = (void *) buf;
	trx = (struct trx_header *) buf;

	/* Look at every 64 KB boundary */
	for (off = *trx_off; off < size; off += (64 * 1024)) {
		memset(buf, 0xe5, sizeof(buf));

		/*
		 * Read block 0 to test for romfs and cramfs superblock
		 */
		if (mtd->read(mtd, off, sizeof(buf), &len, buf) ||
		    len != sizeof(buf))
			continue;

		/* Try looking at TRX header for rootfs offset */
		if (le32_to_cpu(trx->magic) == TRX_MAGIC) {
			*trx_off = off;
			*trx_size = le32_to_cpu(trx->len);
			if (trx->offsets[1] == 0)
				continue;
			/*
			 * Read to test for romfs and cramfs superblock
			 */
			off += le32_to_cpu(trx->offsets[1]);
			memset(buf, 0xe5, sizeof(buf));
			if (mtd->read(mtd, off, sizeof(buf), &len, buf) || len != sizeof(buf))
				continue;
		}

		/* romfs is at block zero too */
		if (romfsb->word0 == ROMSB_WORD0 &&
		    romfsb->word1 == ROMSB_WORD1) {
			printk(KERN_NOTICE
			       "%s: romfs filesystem found at block %d\n",
			       mtd->name, off / mtd->erasesize);
			break;
		}

		/* so is cramfs */
		if (cramfsb->magic == CRAMFS_MAGIC) {
			printk(KERN_NOTICE
			       "%s: cramfs filesystem found at block %d\n",
			       mtd->name, off / mtd->erasesize);
			break;
		}

		if (squashfsb->s_magic == SQUASHFS_MAGIC) {
			printk(KERN_NOTICE
			       "%s: squash filesystem found at block %d\n",
			       mtd->name, off / mtd->erasesize);
			break;
		}
	}

	return off;
}

struct mtd_partition *
init_mtd_partitions(hndsflash_t *sfl_info, struct mtd_info *mtd, size_t size)
{
	struct romfs_super_block *romfsb;
	struct cramfs_super *cramfsb;
	struct squashfs_super_block *squashfsb;
	struct trx_header *trx;
	unsigned char buf[512];
	int off;
	size_t len;
	int i;
	uint32 offset = 0;
	uint rfs_off = 0;
	uint vmlz_off=0, knl_size;
	uint32 trx_size;
#ifdef CONFIG_CRASHLOG
	char create_crash_partition = 0;
#endif

#if 0
	romfsb = (struct romfs_super_block *) buf;
	cramfsb = (struct cramfs_super *) buf;
	squashfsb = (struct squashfs_super_block *) buf;
	trx = (struct trx_header *) buf;
#endif
 
	/* Setup NVRAM MTD partition */
	i = (sizeof(bcm947xx_parts)/sizeof(struct mtd_partition)) - 2;

	bcm947xx_parts[i].size = ROUNDUP(NVRAM_SPACE, mtd->erasesize);
	bcm947xx_parts[i].offset = size - bcm947xx_parts[i].size;

	/* foxconn added start, zacker, 08/19/2010 */
	for (i = i - 1; i > 2; i--)
	{
		if (strncmp(bcm947xx_parts[i].name, "board_data", 10) == 0)
			bcm947xx_parts[i].size = (mtd->erasesize) * 1; /* 64K */
		else if (strncmp(bcm947xx_parts[i].name, "POT", 3) == 0)
			bcm947xx_parts[i].size = (mtd->erasesize) * 1; /* 64K */
		else if (strncmp(bcm947xx_parts[i].name, "T_Meter", 7) == 0)
			bcm947xx_parts[i].size = (mtd->erasesize) * 1; /* 64K */
		else if (strncmp(bcm947xx_parts[i].name, "ML", 2) == 0)
			bcm947xx_parts[i].size = (mtd->erasesize) * 1; /* 64K */
		else
		{
			printk(KERN_ERR "%s: Unknow MTD name %s\n",
			__FUNCTION__, bcm947xx_parts[i].name);
			break;
		}

		bcm947xx_parts[i].offset = bcm947xx_parts[i + 1].offset
									- bcm947xx_parts[i].size;
	}
	/* foxconn added end, zacker, 08/19/2010 */
	/* Size of boot */

		rfs_off = lookup_flash_rootfs_offset(mtd, &vmlz_off, size, &trx_size);
//	bcm947xx_parts[0].size = 256 * 1024;
	bcm947xx_parts[0].size = vmlz_off;
	/* Size of linux */
//	bcm947xx_parts[1].offset = 0;
	bcm947xx_parts[1].offset = vmlz_off;
	
//	bcm947xx_parts[1].size = 0;
	bcm947xx_parts[1].size = mtd->size - vmlz_off;
	/* size of rootfs */
    knl_size = bcm947xx_parts[1].size;
//	bcm947xx_parts[2].offset = 0;
//	bcm947xx_parts[2].size = 0;
	bcm947xx_parts[2].size = knl_size - (rfs_off - vmlz_off);
	bcm947xx_parts[2].offset = rfs_off;
	bcm947xx_parts[1].size -= (12*0X10000);
	bcm947xx_parts[2].size -= (12*0X10000);
#ifdef CONFIG_CRASHLOG
		if ((bcm947xx_flash_parts[nparts].size - trx_size) >=
		    ROUNDUP(0x4000, mtd->erasesize)) {
			bcm947xx_flash_parts[nparts].size -= ROUNDUP(0x4000, mtd->erasesize);
			create_crash_partition = 1;
		} else {
			create_crash_partition = 0;
	
  }  
#endif

	return bcm947xx_parts;
}

EXPORT_SYMBOL(init_mtd_partitions);

#endif /* CONFIG_MTD_PARTITIONS */


#ifdef	CONFIG_MTD_NFLASH
/*Foxconn modify start by Hank 10/24/2012*/
/*add full partition on nandflash for using nandflash boot up*/
#if defined(R6200v2)
#define NFLASH_PARTS_NUM	19
static struct mtd_partition bcm947xx_nflash_parts[NFLASH_PARTS_NUM] = {
	{
		.name = "boot",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
	{
		.name = "nvram",
		.size = 0,
		.offset = 0
	},
	{
		.name = "linux",
		.size = 0,
		.offset = 0
	},
	{
		.name = "rootfs",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
	{ 
		.name = "board_data", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "POT1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "POT2", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "T_Meter1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "T_Meter2", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML2", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML3", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML4", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML5", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML6", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML7", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "R1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "R2", 
		.offset = 0, 
		.size = 0, 
	},
	{
		.name = 0,
		.size = 0,
		.offset = 0
	}
};
#else
#if defined(R7000)
#define NFLASH_PARTS_NUM	18
#else
#define NFLASH_PARTS_NUM	17
#endif
static struct mtd_partition bcm947xx_nflash_parts[NFLASH_PARTS_NUM] = {
	{
		.name = "boot",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
	{
		.name = "nvram",
		.size = 0,
		.offset = 0
	},
#if defined(R7000)
	{
		.name = "linux",
		.size = 0,
		.offset = 0
	},
	{
		.name = "rootfs",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
#endif
	{ 
		.name = "board_data", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "POT1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "POT2", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "T_Meter1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "T_Meter2", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML1", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML2", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML3", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML4", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML5", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML6", 
		.offset = 0, 
		.size = 0, 
	},
	{ 
		.name = "ML7", 
		.offset = 0, 
		.size = 0, 
	},
#if defined(R7000)
	{ 
		.name = "QoSRule", 
		.offset = 0, 
		.size = 0, 
	},
#endif
#if !defined(R7000)
	{
		.name = "linux",
		.size = 0,
		.offset = 0
	},
	{
		.name = "rootfs",
		.size = 0,
		.offset = 0,
		.mask_flags = MTD_WRITEABLE
	},
#endif
	{
		.name = 0,
		.size = 0,
		.offset = 0
	}
};
#endif
/*Foxconn modify end by Hank 10/24/2012*/

static uint
lookup_nflash_rootfs_offset(hndnand_t *nfl, struct mtd_info *mtd, int offset, size_t size)
{
	struct romfs_super_block *romfsb;
	struct cramfs_super *cramfsb;
	struct dummy_super_block *squashfsb;
	struct trx_header *trx;
	unsigned char *buf;
	uint blocksize, pagesize, mask, blk_offset, off, shift = 0;
	int ret;

	pagesize = nfl->pagesize;
	buf = (unsigned char *)kmalloc(pagesize, GFP_KERNEL);
	if (!buf) {
		printk("lookup_nflash_rootfs_offset: kmalloc fail\n");
		return 0;
	}

	romfsb = (struct romfs_super_block *) buf;
	cramfsb = (struct cramfs_super *) buf;
	squashfsb = (void *) buf;
	trx = (struct trx_header *) buf;

	/* Look at every block boundary till 16MB; higher space is reserved for application data. */
	blocksize = mtd->erasesize;
	printk("lookup_nflash_rootfs_offset: offset = 0x%x\n", offset);
	for (off = offset; off < offset + size; off += blocksize) {
		mask = blocksize - 1;
		blk_offset = off & ~mask;
		if (hndnand_checkbadb(nfl, blk_offset) != 0)
			continue;
		memset(buf, 0xe5, pagesize);
		if ((ret = hndnand_read(nfl, off, pagesize, buf)) != pagesize) {
			printk(KERN_NOTICE
			       "%s: nflash_read return %d\n", mtd->name, ret);
			continue;
		}

		/* Try looking at TRX header for rootfs offset */
		if (le32_to_cpu(trx->magic) == TRX_MAGIC) {
			mask = pagesize - 1;
			off = offset + (le32_to_cpu(trx->offsets[1]) & ~mask) - blocksize;
			shift = (le32_to_cpu(trx->offsets[1]) & mask);
			romfsb = (struct romfs_super_block *)((unsigned char *)romfsb + shift);
			cramfsb = (struct cramfs_super *)((unsigned char *)cramfsb + shift);
			squashfsb = (struct squashfs_super_block *)
				((unsigned char *)squashfsb + shift);
			continue;
		}

		/* romfs is at block zero too */
		if (romfsb->word0 == ROMSB_WORD0 &&
		    romfsb->word1 == ROMSB_WORD1) {
			printk(KERN_NOTICE
			       "%s: romfs filesystem found at block %d\n",
			       mtd->name, off / blocksize);
			break;
		}

		/* so is cramfs */
		if (cramfsb->magic == CRAMFS_MAGIC) {
			printk(KERN_NOTICE
			       "%s: cramfs filesystem found at block %d\n",
			       mtd->name, off / blocksize);
			break;
		}

		if (squashfsb->s_magic == SQUASHFS_MAGIC) {
			printk(KERN_NOTICE
			       "%s: squash filesystem with lzma found at block %d\n",
			       mtd->name, off / blocksize);
			break;
		}

	}

	if (buf)
		kfree(buf);

	return shift + off;
}

struct mtd_partition *
init_nflash_mtd_partitions(hndnand_t *nfl, struct mtd_info *mtd, size_t size)
{
	int bootdev;
	int knldev;
	int nparts = 0;
	uint32 offset = 0;
	uint shift = 0;
	uint32 top = 0;
	uint32 bootsz;
	/*Foxconn modify start by Hank 10/24/2012*/
	uint32 partition_size;
	int i=0;
	/*Foxconn modify end by Hank 10/24/2012*/
#ifdef CONFIG_FAILSAFE_UPGRADE
	char *img_boot = nvram_get(BOOTPARTITION);
	char *imag_1st_offset = nvram_get(IMAGE_FIRST_OFFSET);
	char *imag_2nd_offset = nvram_get(IMAGE_SECOND_OFFSET);
	unsigned int image_first_offset = 0;
	unsigned int image_second_offset = 0;
	char dual_image_on = 0;

	/* The image_1st_size and image_2nd_size are necessary if the Flash does not have any
	 * image
	 */
	dual_image_on = (img_boot != NULL && imag_1st_offset != NULL && imag_2nd_offset != NULL);

	if (dual_image_on) {
		image_first_offset = simple_strtol(imag_1st_offset, NULL, 10);
		image_second_offset = simple_strtol(imag_2nd_offset, NULL, 10);
		printk("The first offset=%x, 2nd offset=%x\n", image_first_offset,
			image_second_offset);

	}
#endif	/* CONFIG_FAILSAFE_UPGRADE */

	bootdev = soc_boot_dev((void *)sih);
	knldev = soc_knl_dev((void *)sih);
	/*Foxconn modify end by Hank 10/24/2012*/
	/*Get erase size of nandflash*/
	partition_size = mtd->erasesize;
	/*Foxconn modify end by Hank 10/24/2012*/

	if (bootdev == SOC_BOOTDEV_NANDFLASH) {
		bootsz = boot_partition_size(nfl->base);
		if (bootsz > mtd->erasesize) {
			/* Prepare double space in case of bad blocks */
			/*Foxconn modify start by Hank 10/26/2012*/
			/*change bootcode size*/
			bootsz = (bootsz << 1); 
			/*Foxconn modify end by Hank 10/26/2012*/
		} else {
			/* CFE occupies at least one block */
			bootsz = mtd->erasesize;
		}
		printk("Boot partition size = %d(0x%x)\n", bootsz, bootsz);

		/* Size pmon */
		/*Foxconn modify start by Hank 10/24/2012*/
		/*set bootcode and nvram size and offset*/
		bcm947xx_nflash_parts[0].name = "boot";
		/*Foxconn modify start by Hank 10/24/2012*/
		/*change to dynamic size of bootcode*/
		bcm947xx_nflash_parts[0].size = bootsz;
		/*Foxconn modify end by Hank 10/24/2012*/
		bcm947xx_nflash_parts[0].offset = top;
		bcm947xx_nflash_parts[0].mask_flags = MTD_WRITEABLE; /* forces on read only */
		offset = bcm947xx_nflash_parts[0].size;

		/*Foxconn modify start by Hank 10/24/2012*/
		/*change to dynamic size of nvram*/
		bcm947xx_nflash_parts[1].size = NFL_BOOT_SIZE - bootsz;
		bcm947xx_nflash_parts[1].offset = bcm947xx_nflash_parts[0].size;
		/*Foxconn modify end by Hank 10/24/2012*/
		/*remove older partition setting*/
#if 0
		bcm947xx_nflash_parts[nparts].name = "boot";
		bcm947xx_nflash_parts[nparts].size = bootsz;
		bcm947xx_nflash_parts[nparts].offset = top;
		bcm947xx_nflash_parts[nparts].mask_flags = MTD_WRITEABLE; /* forces on read only */
		offset = bcm947xx_nflash_parts[nparts].size;
		nparts++;

		/* Setup NVRAM MTD partition */
		bcm947xx_nflash_parts[nparts].name = "nvram";
		bcm947xx_nflash_parts[nparts].size = NFL_BOOT_SIZE - offset;
		bcm947xx_nflash_parts[nparts].offset = offset;

		offset = NFL_BOOT_SIZE;
		nparts++;
#endif
		/*Foxconn modify end by Hank 10/24/2012*/
	}

	/*Foxconn modify start by Hank 10/24/2012*/
	if (knldev == SOC_KNLDEV_NANDFLASH) {

#if defined(R6200v2)
		/* Size of linux */
		bcm947xx_nflash_parts[2].offset = bcm947xx_nflash_parts[1].offset + bcm947xx_nflash_parts[1].size;
		bcm947xx_nflash_parts[2].size = NFL_BOOT_OS_SIZE; /* reserved 32 MB for firmware */ //NFL_BOOT_OS_SIZE - bcm947xx_nflash_parts[2].offset ;
	
		shift = lookup_nflash_rootfs_offset(nfl, mtd, bcm947xx_nflash_parts[2].offset,
				bcm947xx_nflash_parts[2].size );
	
		/* size of rootfs */
		bcm947xx_nflash_parts[3].offset = shift;
		bcm947xx_nflash_parts[3].size = (bcm947xx_nflash_parts[2].offset + bcm947xx_nflash_parts[2].size) - shift;
			
		/* Setup other MTD partition */
		for (i = 4; i < 18; i++){
			bcm947xx_nflash_parts[i].size = partition_size*2; /* reserved 2 block for each partition */
			bcm947xx_nflash_parts[i].offset = bcm947xx_nflash_parts[i-1].offset + bcm947xx_nflash_parts[i-1].size;
		}
#elif defined(R7000)
		bcm947xx_nflash_parts[2].offset = NFL_BOOT_SIZE;
		bcm947xx_nflash_parts[2].size = NFL_BOOT_OS_SIZE;

		shift = lookup_nflash_rootfs_offset(nfl, mtd, bcm947xx_nflash_parts[2].offset,	bcm947xx_nflash_parts[2].size );

		bcm947xx_nflash_parts[3].offset = shift;
		bcm947xx_nflash_parts[3].size = (bcm947xx_nflash_parts[2].offset + bcm947xx_nflash_parts[2].size) - shift;
	
		/* size of rootfs */
		/* Setup other MTD partition */
		for (i = 4; i < 17; i++){
                     if(i==7 ||i==8)
			bcm947xx_nflash_parts[i].size = (partition_size <<2 );
                     else
			bcm947xx_nflash_parts[i].size = (partition_size <<1 );
		     bcm947xx_nflash_parts[i].offset = bcm947xx_nflash_parts[i-1].offset + bcm947xx_nflash_parts[i-1].size;
		}
	
		/* Size of linux */
	
#else	
		/* Setup other MTD partition */
		for (i = 2; i < 14; i++){
			bcm947xx_nflash_parts[i].size = partition_size;
			bcm947xx_nflash_parts[i].offset = bcm947xx_nflash_parts[i-1].offset + bcm947xx_nflash_parts[i-1].size;
		}
	
		/* Size of linux */
		bcm947xx_nflash_parts[14].offset = bcm947xx_nflash_parts[i-1].offset + bcm947xx_nflash_parts[i-1].size;
		bcm947xx_nflash_parts[14].size = NFL_BOOT_OS_SIZE - bcm947xx_nflash_parts[2].offset ;
	
		shift = lookup_nflash_rootfs_offset(nfl, mtd, bcm947xx_nflash_parts[14].offset,
				bcm947xx_nflash_parts[14].size );
	
		/* size of rootfs */
		bcm947xx_nflash_parts[15].offset = shift;
		bcm947xx_nflash_parts[15].size = (bcm947xx_nflash_parts[14].offset + bcm947xx_nflash_parts[14].size) - shift;
#endif /* defined(R6200v2) */
		/*remove older partition setting*/
#if 0
		/* Setup kernel MTD partition */
		bcm947xx_nflash_parts[nparts].name = "linux";
#ifdef CONFIG_FAILSAFE_UPGRADE
		if (dual_image_on) {
			bcm947xx_nflash_parts[nparts].size =
				image_second_offset - image_first_offset;
		} else
#endif
		{
			bcm947xx_nflash_parts[nparts].size =
				nparts ? (NFL_BOOT_OS_SIZE - NFL_BOOT_SIZE) : NFL_BOOT_OS_SIZE;
		}
		bcm947xx_nflash_parts[nparts].offset = offset;

		shift = lookup_nflash_rootfs_offset(nfl, mtd, offset,
			bcm947xx_nflash_parts[nparts].size);

#ifdef CONFIG_FAILSAFE_UPGRADE
		if (dual_image_on)
			offset = image_second_offset;
		else
#endif
		offset = NFL_BOOT_OS_SIZE;
		nparts++;

		/* Setup rootfs MTD partition */
		bcm947xx_nflash_parts[nparts].name = "rootfs";
#ifdef CONFIG_FAILSAFE_UPGRADE
		if (dual_image_on)
			bcm947xx_nflash_parts[nparts].size = image_second_offset - shift;
		else
#endif
		bcm947xx_nflash_parts[nparts].size = NFL_BOOT_OS_SIZE - shift;
		bcm947xx_nflash_parts[nparts].offset = shift;
		bcm947xx_nflash_parts[nparts].mask_flags = MTD_WRITEABLE;
		offset = NFL_BOOT_OS_SIZE;

		nparts++;

#ifdef CONFIG_FAILSAFE_UPGRADE
		/* Setup 2nd kernel MTD partition */
		if (dual_image_on) {
			bcm947xx_nflash_parts[nparts].name = "linux2";
			bcm947xx_nflash_parts[nparts].size = NFL_BOOT_OS_SIZE - image_second_offset;
			bcm947xx_nflash_parts[nparts].offset = image_second_offset;
			shift = lookup_nflash_rootfs_offset(nfl, mtd, image_second_offset,
			                                    bcm947xx_nflash_parts[nparts].size);
			nparts++;
			/* Setup rootfs MTD partition */
			bcm947xx_nflash_parts[nparts].name = "rootfs2";
			bcm947xx_nflash_parts[nparts].size = NFL_BOOT_OS_SIZE - shift;
			bcm947xx_nflash_parts[nparts].offset = shift;
			bcm947xx_nflash_parts[nparts].mask_flags = MTD_WRITEABLE;
			nparts++;
		}
#endif	/* CONFIG_FAILSAFE_UPGRADE */
#endif
	/*Foxconn modify end by Hank 10/24/2012*/
	}

#ifdef PLAT_NAND_JFFS2
	/* Setup the remainder of NAND Flash as FFS partition */
	if( size > offset ) {
		bcm947xx_nflash_parts[nparts].name = "ffs";
		bcm947xx_nflash_parts[nparts].size = size - offset ;
		bcm947xx_nflash_parts[nparts].offset = offset;
		bcm947xx_nflash_parts[nparts].mask_flags = 0;
		bcm947xx_nflash_parts[nparts].ecclayout = mtd->ecclayout;
		nparts++;
	}
#endif

	return bcm947xx_nflash_parts;
}

/* LR: Calling this function directly violates Linux API conventions */
EXPORT_SYMBOL(init_nflash_mtd_partitions);
#else

/* Note: NFL_BOOT_OS_SIZE is now set to 32M for FAILSAFE issue */
#ifndef	NFL_BOOT_OS_SIZE
#define	NFL_BOOT_OS_SIZE	SZ_32M
#endif

/* LR: Here is how partition managers are intended to be */
static int parse_cfenand_partitions(struct mtd_info *mtd,
                             struct mtd_partition **pparts,
                             unsigned long origin)
{
	unsigned size, offset, nparts ;
	static struct mtd_partition cfenand_parts[2] = {{0}};

	if ( pparts == NULL )
		return -EINVAL;

	/*
	NOTE:
	Can not call init_nflash_mtd_partitions)_ because it depends
	on the "other" conflicting driver for its operation,
	so instead this will just create two partitions
	so that jffs2 does not overwrite bootable areas.
	*/

	size = mtd->size ;
	nparts = 0;
	offset = origin ;

	printk("%s: slicing up %uMiB provisionally\n", __func__, size >> 20 );

	cfenand_parts[nparts].name = "cfenand";
	cfenand_parts[nparts].size = NFL_BOOT_OS_SIZE ;
	cfenand_parts[nparts].offset = offset ;
	cfenand_parts[nparts].mask_flags = MTD_WRITEABLE;
	cfenand_parts[nparts].ecclayout = mtd->ecclayout;
	nparts++;
	offset += NFL_BOOT_OS_SIZE ;

	cfenand_parts[nparts].name = "jffs2";
	cfenand_parts[nparts].size = size - offset ;
	cfenand_parts[nparts].offset = offset;
	cfenand_parts[nparts].mask_flags = 0;
	cfenand_parts[nparts].ecclayout = mtd->ecclayout;
	nparts++;

	*pparts = cfenand_parts;
	return nparts;
}

static struct mtd_part_parser cfenand_parser = {
        .owner = THIS_MODULE,
        .parse_fn = parse_cfenand_partitions,
        .name = "cfenandpart",
};

static int __init cfenenad_parser_init(void)
{
        return register_mtd_parser(&cfenand_parser);
}

module_init(cfenenad_parser_init);

#endif /* CONFIG_MTD_NFLASH */

#ifdef CONFIG_CRASHLOG
extern char *get_logbuf(void);
extern char *get_logsize(void);

void nvram_store_crash(void)
{
	struct mtd_info *mtd = NULL;
	int i;
	char *buffer;
	unsigned char buf[16];
	int buf_len;
	int len;

	printk("Trying to store crash\n");

	mtd = get_mtd_device_nm("crash");

	if (!IS_ERR(mtd)) {

		buf_len = get_logsize();
		buffer = get_logbuf();
		if (buf_len > mtd->size)
			buf_len = mtd->size;

		memset(buf,0,sizeof(buf));
		mtd->read(mtd, 0, sizeof(buf), &len, buf);
		for (len=0;len<sizeof(buf);len++)
			if (buf[len]!=0xff) {
				printk("Could not save crash, partition not clean\n");
				break;
			}
		if (len == sizeof(buf)) {
			mtd->write(mtd, 0, buf_len, &len, buffer);
			if (buf_len == len)
				printk("Crash Saved\n");
		}
	} else {
		printk("Could not find NVRAM partition\n");
	}
}
#endif /* CONFIG_CRASHLOG */
