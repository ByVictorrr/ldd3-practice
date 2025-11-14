#ifndef MINTBUS_H
#define MINTBUS_H
#include <linux/device.h>
#include <linux/module.h>

#define MAX_MINT_ID_LEN 32
struct mint_id {
    // const char name[32];
    char name[MAX_MINT_ID_LEN];
};

struct mint_dev
{
    struct mint_id id;
    struct device device;
    void *priv_data;

};
struct mint_driver {
    char *name;
    const struct mint_id *id_table;
    int (*probe)(struct mint_dev *);
    void (*remove)(struct mint_dev *);
    struct device_driver driver;

};
inline struct mint_dev *to_mint_device(struct device *d)
{
    return container_of(d, struct mint_dev, device);
}

inline struct mint_driver *to_mint_driver(struct device_driver *drv)
{
    return container_of(drv, struct mint_driver, driver);
}
int mint_core_probe(struct device *dev);

int mint_core_remove(struct device *dev);


int mint_register_driver(struct mint_driver *drv);

void mint_unregister_driver(struct mint_driver *drv);

extern struct bus_type mint_bus;


#endif