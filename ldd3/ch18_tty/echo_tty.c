#include <linux/init.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>

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

};
struct echo_dev *links;

static int activate_tty(struct tty_port *port, struct tty_struct *tty)
{

    // called on first open
    // turn on interrupts, activate hw
    struct echo_dev *dev = container_of(port, struct echo_dev, port);
    spin_lock(&port->lock);
    dev->connected = true;
    // memset(dev->tx_fifo, 0, sizeof(dev->buffer));
    spin_unlock(&port->lock);
    printk(KERN_INFO "ttyECHO: activate\n");
    return 0;
}

static void shutdown_tty(struct tty_port *port)
{
    // called on last close
    // turn off interrupts, deactivate hw

    struct echo_dev *dev = container_of(port, struct echo_dev, port);
    spin_lock(&port->lock);
    dev->connected = false;
    // memset(dev->buffer, 1, sizeof(dev->buffer));
    spin_unlock(&port->lock);
    printk(KERN_INFO "ttyECHO: shutdown\n");
}
static const struct tty_port_operations port_ops = {
    .activate = activate_tty,
    .shutdown = shutdown_tty,

};
static int my_link_open(struct tty_struct *tty, struct file *filp)
{
    printk(KERN_INFO "ttyECHO: open\n");
    // tty->index is just register one
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
    if (!dev->connected)
        return 0;
    return kfifo_avail(&dev->tx_fifo);
}
static unsigned int chars_in_buffer(struct tty_struct *tty)
{
    struct echo_dev *dev = tty->driver_data;
    return kfifo_len(&dev->tx_fifo);
}
static void transmit_work(struct work_struct *work)
{
    struct echo_dev *dev = container_of(work, struct echo_dev, tx_work);
    unsigned int n;
    unsigned char tmp[MAX_BUFFER];
    if (!dev->connected)
        return;
    for (;;)
    {
         n = kfifo_out_spinlocked(&dev->tx_fifo, tmp, sizeof(tmp), &dev->lock);
        if (!n)
            break;
        // transmit for echo dmeo feed rx
        tty_insert_flip_string(&dev->port, tmp, n);
        tty_flip_buffer_push(&dev->port);


        // TODO drain
    }
    struct tty_struct *tty = tty_port_tty_get(&dev->port);
    tty_wakeup(tty); // wait queue waiting for more data

}
static ssize_t my_link_write(struct tty_struct *tty, const unsigned char *buf, size_t count)
{
    struct echo_dev *dev = tty->driver_data;
    int i;
    if (!dev->connected)
        return -EIO;
    // serialized access when called
    unsigned int n = kfifo_in_spinlocked(&dev->tx_fifo, buf, count, &dev->lock);
    if (n)
        schedule_work(&dev->tx_work);
    return n ? n: -EAGAIN;

}
struct tty_operations tty_ops = {
    .open = my_link_open,
    .write = my_link_write,
    .close = my_link_close,
    .chars_in_buffer = chars_in_buffer,
    .write_room = write_room,

};

int __init tty_init(void){
    int retval=0, i;
    // allocate tty_driver for 2 devices
    tty_driver = tty_alloc_driver(MAX_PORTS, TTY_DRIVER_DYNAMIC_DEV);
    // initalize the tty_driver fields
    tty_driver->driver_name = "my_tty"; // module name
    tty_driver->name = "ttyECHO"; // dev node name for the port
    tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
    tty_driver->subtype = SERIAL_TYPE_NORMAL;
    // standard default
    tty_driver->init_termios = tty_std_termios;
    // CLOCAL only one open at a time and no blocking`
    tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;;
    tty_set_operations(tty_driver, &tty_ops);
    // register the driver
    retval = tty_register_driver(tty_driver);
    if (retval)
    {
        printk(KERN_ERR "tty_register_driver failed\n");
        tty_driver_kref_put(tty_driver);
        return retval;
    }
    // allocate and init the ports
    links = kzalloc(sizeof(struct echo_dev)*MAX_PORTS, GFP_KERNEL);
    for (i=0; i<MAX_PORTS; i++)
    {
        struct echo_dev *dev = &links[i];
        tty_port_init(&dev->port); // init
        links[i].connected = false;
        spin_lock_init(&dev->lock);
        // INIT_WORK(dev->tx_work, )
        kfifo_alloc(&dev->tx_fifo, MAX_BUFFER, GFP_KERNEL);
        dev->port.ops = &port_ops; //close/open behavior
        // register the dev node
        dev->dev = tty_port_register_device(&links[i].port, tty_driver, i, NULL);
        if (!IS_ERR(dev->dev)){
            continue;
        }
        // on error, unwinid
        while (--i>=0)
        {
            tty_unregister_device(tty_driver, i) ;
            tty_port_destroy(&dev->port);
        }

        tty_unregister_driver(tty_driver);
        tty_driver_kref_put(tty_driver);
        kfree(links);
        retval = PTR_ERR(dev->dev);
   }



    return retval;

}

static void __exit tty_exit(void)
{
    int i;
    // remove each device
    for (i=0; i<MAX_PORTS; i++)
    {
        tty_unregister_device(tty_driver, i) ; // undo device node register
        tty_port_destroy(&links[i].port); // give back port
    }
    tty_unregister_driver(tty_driver); // unregister driver
    tty_driver_kref_put(tty_driver); // free driver
    kfree(links);
}
module_init(tty_init);
module_exit(tty_exit);
