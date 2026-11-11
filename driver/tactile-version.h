/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The kernel's own hid-magicmouse has no version. This copy gets one, so that
 * DKMS, `modinfo` and /sys/module/hid_magicmouse/version can tell the two
 * apart. It is added from the Makefile rather than in hid-magicmouse.c, which
 * stays exactly the kernel's file with the two patches applied.
 *
 * Raise it whenever the driver changes.
 */
#include <linux/module.h>

MODULE_VERSION("0.2.0");
