#ifndef _SCULL_IOCTL_H_
#define _SCULL_IOCTL_H_
#pragma once
#define SCULL_IOC_MAGIC 'k' /* MAGIC Number representing a scull ioctl cmd */
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0)
#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC, 1, int)
#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC, 2, int)

#define SCULL_IOCSQSET _IOW(SCULL_IOC_MAGIC, 3, int)
#define SCULL_IOCGQSET _IOR(SCULL_IOC_MAGIC, 4, int)



#endif