/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __LINUX_ENDIAN_DOT_H__
#define __LINUX_ENDIAN_DOT_H__


#ifdef __KERNEL__
#error "don't use linux_endian.h in kernel space under Linux"
#endif

#include <endian.h>
#include <byteswap.h>


#if __BYTE_ORDER == __BIG_ENDIAN

#define be16_to_cpu(x) (x)
#define be32_to_cpu(x) (x)
#define be64_to_cpu(x) (x)

#define cpu_to_be16(x) (x)
#define cpu_to_be32(x) (x)
#define cpu_to_be64(x) (x)

#define le16_to_cpu(x) (bswap_16((x)))
#define le32_to_cpu(x) (bswap_32((x)))
#define le64_to_cpu(x) (bswap_64((x)))

#define cpu_to_le16(x) (bswap_16((x)))
#define cpu_to_le32(x) (bswap_32((x)))
#define cpu_to_le64(x) (bswap_64((x)))

#endif /* __BYTE_ORDER == __BIG_ENDIAN */


#if __BYTE_ORDER == __LITTLE_ENDIAN

#define be16_to_cpu(x) (bswap_16((x)))
#define be32_to_cpu(x) (bswap_32((x)))
#define be64_to_cpu(x) (bswap_64((x)))

#define cpu_to_be16(x) (bswap_16((x)))
#define cpu_to_be32(x) (bswap_32((x)))
#define cpu_to_be64(x) (bswap_64((x))) 

#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)

#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)

#endif /* __BYTE_ORDER == __LITTLE_ENDIAN */


#endif /* __LINUX_ENDIAN_DOT_H__ */
