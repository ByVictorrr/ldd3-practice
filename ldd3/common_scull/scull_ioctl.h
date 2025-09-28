#ifndef _SCULL_IOCTL_H_
#define _SCULL_IOCTL_H_
#pragma once
#define SCULL_IOC_MAGIC 'k' /* MAGIC Number representing a scull ioctl cmd */
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0) /* reset to defaults */

#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC, 1, int) /* "Set" quantum via pointer */
#define SCULL_IOCSQSET _IOW(SCULL_IOC_MAGIC, 2, int) /* "Set" qset via pointer */

#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC, 3)  /* "Tell" quantum via arg value */
#define SCULL_IOCTQSET _IO(SCULL_IOC_MAGIC, 4)  /* "Tell" qset via arg value */

#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC, 5, int)  /* "Get" Quantum via pointer */
#define SCULL_IOCGQSET _IOR(SCULL_IOC_MAGIC, 6, int)  /* "Get" QSet via pointer */

#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC, 7) /* "Query" quantum via return value */
#define SCULL_IOCQQSET _IO(SCULL_IOC_MAGIC, 8) /* "Query" qset via return value */

#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int) /* "eXchange" quantum - atomic get and set */
#define SCULL_IOCXQSET _IOWR(SCULL_IOC_MAGIC, 10, int) /* "eXchange" qset - atomic get and set */
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC, 11) /* "sHift" - toggling behavior */
#define SCULL_IOCHQSET _IO(SCULL_IOC_MAGIC, 12) /* "sHift" - toggling behavior */
#define SCULL_IOC_MAXNR 14

#endif