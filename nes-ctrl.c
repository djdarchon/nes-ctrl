#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <linux/proc_fs.h>
#include <linux/slab.h>

#include <asm/io.h>

#define LLL_MAX_USER_SIZE 1024

static struct proc_dir_entry *nes_proc = NULL;

static unsigned int *gpio_registers = NULL;

static const unsigned int PIN_P1_CLK = 12;
static const unsigned int PIN_P1_LATCH = 6;
static const unsigned int PIN_P1_DATA = 5;

static const unsigned int PIN_P2_CLK = 21;
static const unsigned int PIN_P2_LATCH = 20;
static const unsigned int PIN_P2_DATA = 26;

static const unsigned int OUTPUT = 1;
static const unsigned int INPUT = 0;

static const unsigned int PULLUP = 1;
static const unsigned int PULLDOWN = 0;

static void gpio_pin_io(unsigned int pin, unsigned int output)
{
	unsigned int fsel_index = pin/10;
	unsigned int fsel_bitpos = pin%10;
	unsigned int* gpio_fsel = gpio_registers + fsel_index;
	unsigned int update = ioread32(gpio_fsel);

	update &= (~(0x7 << (fsel_bitpos*3))); // mask off the bit in question
	update |= ((output == OUTPUT ? 1 : 0) << (fsel_bitpos*3)); // or in the output status
	iowrite32(update, gpio_fsel);

	return;
}

static void gpio_pin_pupd(unsigned int pin, unsigned int pupd)
{
	unsigned int pupd_index = pin/16;
	unsigned int pupd_bitpos = pin%16;
	unsigned int* gpio_pupd = ((unsigned int*) ((char*)gpio_registers + 0xE4)) + pupd_index;
	unsigned int update = ioread32(gpio_pupd);

	update &= (~(0x3 << (pupd_bitpos*2)));
	update |= (pupd == PULLUP ? 0x1 : 0x2) << (pupd_bitpos*2);
	iowrite32(update, gpio_pupd);

	return;
}

static void gpio_pin_write(unsigned int pin, unsigned int value)
{
	unsigned int *gpio_on_register = (unsigned int*)((char*)gpio_registers + 0x1c);
	unsigned int *gpio_off_register = (unsigned int*)((char*)gpio_registers + 0x28);
	unsigned int update;

	if (value == 0)
	{
		update = (1 << pin);
		iowrite32(update, gpio_off_register);
	}
	else
	{
		update = (1 << pin);
		iowrite32(update, gpio_on_register);
	}

	return;
}

static unsigned int gpio_pin_read(unsigned int pin)
{
	unsigned int *gpio_lev_register = (unsigned int*)((char*)gpio_registers + 0x34);
	unsigned int value = ioread32(gpio_lev_register);
	return ((value >> pin) & 0x1);
}

void nes_init(void)
{
	gpio_pin_io(PIN_P1_CLK, OUTPUT);
	gpio_pin_io(PIN_P1_LATCH, OUTPUT);
	gpio_pin_io(PIN_P1_DATA, INPUT);
	gpio_pin_pupd(PIN_P1_DATA, PULLDOWN); 

	gpio_pin_io(PIN_P2_CLK, OUTPUT);
	gpio_pin_io(PIN_P2_LATCH, OUTPUT);
	gpio_pin_io(PIN_P2_DATA, INPUT);
	gpio_pin_pupd(PIN_P2_DATA, PULLDOWN); 

	gpio_pin_write(PIN_P1_CLK, 1);
	gpio_pin_write(PIN_P1_LATCH, 0);
	gpio_pin_write(PIN_P2_CLK, 1);
	gpio_pin_write(PIN_P2_LATCH, 0);
	return;
}

static unsigned int LATCH_HIGH = 6;
static unsigned int LATCH_LOW = 6;
static unsigned int CLK_HIGH = 6;
static unsigned int CLK_LOW = 6;

ssize_t nes_read(struct file *file, char __user *user, size_t size, loff_t *off)
{
	unsigned int i;
	unsigned int p1, p2, result;
	char buffer[5];

	// first, set latch
	gpio_pin_write(PIN_P1_LATCH, 1);
	gpio_pin_write(PIN_P2_LATCH, 1);
	udelay(LATCH_HIGH);
	gpio_pin_write(PIN_P1_LATCH, 0);
	gpio_pin_write(PIN_P2_LATCH, 0);
	p1 = (gpio_pin_read(PIN_P1_DATA) & 0x1);
	p2 = (gpio_pin_read(PIN_P2_DATA) & 0x1);
	udelay(LATCH_LOW);

	// do the clock thing
	for (i=0; i < 8; i=i+1)
	{
		udelay(CLK_HIGH);
		p1 = (p1 << 1) | (gpio_pin_read(PIN_P1_DATA) & 0x1);
		p2 = (p2 << 1) | (gpio_pin_read(PIN_P2_DATA) & 0x1);
		gpio_pin_write(PIN_P1_CLK, 0);
		gpio_pin_write(PIN_P2_CLK, 0);
		udelay(CLK_LOW);
		gpio_pin_write(PIN_P1_CLK, 1);
		gpio_pin_write(PIN_P2_CLK, 1);
	}

	result = (~((p1 << 8) | (0xFF & p2))) & 0xFFFF;
	sprintf(buffer, "%04x\n", result);
	return copy_to_user(user,buffer, sizeof(buffer)) ? 0 : sizeof(buffer);
}

ssize_t nes_write(struct file *file, const char __user *user, size_t size, loff_t *off)
{
	return size;
}

static const struct proc_ops nes_proc_fops = 
{
	.proc_read = nes_read,
	.proc_write = nes_write,
};


static int __init gpio_driver_init(void)
{
	gpio_registers = (int*)ioremap(0xFE000000 + 0x200000, PAGE_SIZE);
	if (gpio_registers == NULL)
	{
		printk("Failed to map GPIO memory to driver\n");
		return -1;
	}
	
	printk("Successfully mapped in GPIO memory\n");
	
	// create an entry in the proc-fs
	nes_proc = proc_create("nes-ctrl", 0666, NULL, &nes_proc_fops);
	if (nes_proc == NULL)
	{
		return -1;
	}

	// initialize pin settings
	nes_init();

	return 0;
}

static void __exit gpio_driver_exit(void)
{
	iounmap(gpio_registers);
	proc_remove(nes_proc);
	return;
}

module_init(gpio_driver_init);
module_exit(gpio_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Low Level Learning, Jaden Darchon");
MODULE_DESCRIPTION("Build from LLL codebase; NES controller drive");
MODULE_VERSION("1.0");
