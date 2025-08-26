//
// Created by victord on 8/24/25.
//

#ifndef LDD3_PRACTICE_DEBUG_H
#define LDD3_PRACTICE_DEBUG_H

#ifdef SCULL_DEBUG
#ifdef __KERNEL__
# define PDEBUG(fmt, args...) printk(KERN_DEBUG "DEBUG: " fmt, ## args)
#else
# define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#endif
#else
# define PDEBUG(fmt, args...) /* turn off print */
#endif //LDD3_PRACTICE_DEBUG_H