#include <linux/init.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>

/* ADD: these headers are required for the things you’re using */
#include <linux/module.h>      // MODULE_LICENSE, module_init/exit
#include <linux/slab.h>        // kzalloc/kfree
#include <linux/kfifo.h>       // struct kfifo + helpers
#include <linux/workqueue.h>   // work_struct, INIT_WORK, schedule_work
#include <linux/spinlock.h>    // spinlock_t
#include <linux/device.h>      // struct device

struct tty_driver *tty_driver;
#define MAX_BUFFER 100
#define MAX_PORTS 2
#define MAX_MINORS 4

struct echo_dev
{
    struct device *dev;
    struct tty_port port;
    spinlock_t lock;
    bool connected; // emulate some hw that might be touched in interrupt
    struct kfifo tx_fifo; // bytes waiting to be sent
    struct work_struct tx_work; // bh transmitter
    bool stopped;
};

struct echo_dev *links;

static int activate_tty(struct tty_port *port, struct tty_struct *tty)
{
    struct echo_dev *dev = container_of(port, struct echo_dev, port);

    spin_lock(&dev->lock);
    dev->connected = true;
    spin_unlock(&dev->lock);

    printk(KERN_INFO "ttyECHO: activate\n");
    return 0;
}

static void shutdown_tty(struct tty_port *port)
{
    struct echo_dev *dev = container_of(port, struct echo_dev, port);

    spin_lock(&dev->lock);
    dev->connected = false;
    spin_unlock(&dev->lock);

    printk(KERN_INFO "ttyECHO: shutdown\n");
}

static const struct tty_port_operations port_ops = {
    .activate = activate_tty,
    .shutdown = shutdown_tty,
};

static int my_link_open(struct tty_struct *tty, struct file *filp)
{
    printk(KERN_INFO "ttyECHO: open\n");
    struct echo_dev *dev = &links[tty->index];
    tty->driver_data = dev;
    return tty_port_open(&dev->port, tty, filp);
}

static void my_link_close(struct tty_struct *tty, struct file *filp)
{
    struct echo_dev *dev = tty->driver_data;
    tty_port_close(&dev->port, tty, filp);
}

static unsigned int write_room(struct tty_struct *tty)
{
    struct echo_dev *dev = tty->driver_data;
    if (!dev || !dev->connected)
        return 0;
    return kfifo_avail(&dev->tx_fifo);
}

static unsigned int chars_in_buffer(struct tty_struct *tty)
{
    struct echo_dev *dev = tty->driver_data;
    if (!dev)
        return 0;
    return kfifo_len(&dev->tx_fifo);
}

static void transmit_work(struct work_struct *work)
{
    struct echo_dev *dev = container_of(work, struct echo_dev, tx_work);
    unsigned int n;
    unsigned char tmp[MAX_BUFFER];
    spin_lock(&dev->lock);
    if (!dev->connected || dev->stopped)
    {
        spin_unlock(&dev->lock);
        return;

    }

    spin_unlock(&dev->lock);

    for (;;) {
        n = kfifo_out_spinlocked(&dev->tx_fifo, tmp, sizeof(tmp), &dev->lock);
        if (!n)
            break;

        /* transmit for echo demo: feed rx */
        tty_insert_flip_string(&dev->port, tmp, n);
        tty_flip_buffer_push(&dev->port);
    }

    struct tty_struct *tty = tty_port_tty_get(&dev->port);
    if (tty) {
        tty_wakeup(tty);      // wake writers waiting for room
        tty_kref_put(tty);    // release ref
    }
}
static void tty_start(struct tty_struct *tty)
{
    struct echo_dev *dev = tty->driver_data;
    spin_lock(&dev->lock);
    dev->stopped = false;
    spin_unlock(&dev->lock);
    if (chars_in_buffer(tty))
        schedule_work(&dev->tx_work);

}
static void tty_stop(struct tty_struct *tty)
{
    /* TX stop */
    struct echo_dev *dev = tty->driver_data;
    spin_lock(&dev->lock);
    dev->stopped = true;
    spin_unlock(&dev->lock);
    cancel_work_sync(&dev->tx_work);

}

static ssize_t my_link_write(struct tty_struct *tty,
                             const unsigned char *buf, size_t count)
{
    struct echo_dev *dev = tty->driver_data;

    if (!dev || !dev->connected || dev->stopped)
        return -EIO;

    unsigned int n = kfifo_in_spinlocked(&dev->tx_fifo, buf, count, &dev->lock);

    if (n)
        schedule_work(&dev->tx_work);

    return n ? n : -EAGAIN;
}

struct tty_operations tty_ops = {
    .open = my_link_open,
    .write = my_link_write,
    .close = my_link_close,
    .chars_in_buffer = chars_in_buffer,
    .write_room = write_room,
    /* for async flow control */
    .start = tty_start,
    .stop = tty_stop,
};

int __init tty_init(void)
{
    int retval = 0, i;

    tty_driver = tty_alloc_driver(MAX_PORTS, TTY_DRIVER_DYNAMIC_DEV);
    /* ADD: tty_alloc_driver can fail */
    if (IS_ERR(tty_driver))
        return PTR_ERR(tty_driver);

    tty_driver->driver_name = "my_tty";
    tty_driver->name = "ttyECHO";
    tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
    tty_driver->subtype = SERIAL_TYPE_NORMAL;
    tty_driver->init_termios = tty_std_termios;
    tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;

    tty_set_operations(tty_driver, &tty_ops);

    retval = tty_register_driver(tty_driver);
    if (retval) {
        printk(KERN_ERR "tty_register_driver failed\n");
        tty_driver_kref_put(tty_driver);
        return retval;
    }

    links = kzalloc(sizeof(struct echo_dev) * MAX_PORTS, GFP_KERNEL);
    if (!links) {
        retval = -ENOMEM;
        goto err_unreg_driver;
    }

    for (i = 0; i < MAX_PORTS; i++) {
        struct echo_dev *dev = &links[i];

        tty_port_init(&dev->port);
        dev->stopped = false;
        dev->connected = false;
        spin_lock_init(&dev->lock);

        INIT_WORK(&dev->tx_work, transmit_work);

        retval = kfifo_alloc(&dev->tx_fifo, MAX_BUFFER, GFP_KERNEL);
        if (retval)
            goto err_unwind;

        dev->port.ops = &port_ops;

        dev->dev = tty_port_register_device(&dev->port, tty_driver, i, NULL);
        if (IS_ERR(dev->dev)) {
            retval = PTR_ERR(dev->dev);
            kfifo_free(&dev->tx_fifo);   // ADD: avoid leak
            goto err_unwind;
        }
    }

    return 0;

err_unwind:
    while (--i >= 0) {
        tty_unregister_device(tty_driver, i);
        cancel_work_sync(&links[i].tx_work);
        kfifo_free(&links[i].tx_fifo);
        tty_port_destroy(&links[i].port);
    }
    kfree(links);

err_unreg_driver:
    tty_unregister_driver(tty_driver);
    tty_driver_kref_put(tty_driver);
    return retval;
}

static void __exit tty_exit(void)
{
    int i;

    for (i = 0; i < MAX_PORTS; i++) {
        tty_unregister_device(tty_driver, i);

        /* ADD: clean up work + fifo */
        cancel_work_sync(&links[i].tx_work);
        kfifo_free(&links[i].tx_fifo);

        tty_port_destroy(&links[i].port);
    }

    tty_unregister_driver(tty_driver);
    tty_driver_kref_put(tty_driver);
    kfree(links);
}

module_init(tty_init);
module_exit(tty_exit);

MODULE_LICENSE("GPL");
