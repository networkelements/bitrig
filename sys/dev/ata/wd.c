/*	$OpenBSD: wd.c,v 1.16 2001/04/30 21:19:45 csapuntz Exp $ */
/*	$NetBSD: wd.c,v 1.193 1999/02/28 17:15:27 explorer Exp $ */

/*
 * Copyright (c) 1998 Manuel Bouyer.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *	must display the following acknowledgement:
 *  This product includes software developed by Manuel Bouyer.
 * 4. The name of the author may not be used to endorse or promote products
 *	derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Onno van der Linden.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if 0
#include "rnd.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#if NRND > 0
#include <sys/rnd.h>
#endif
#include <sys/vnode.h>

#include <vm/vm.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/ata/atareg.h>
#include <dev/ata/atavar.h>
#include <dev/ata/wdvar.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcvar.h>
#if 0
#include "locators.h"
#endif

#define	WDIORETRIES_SINGLE 4	/* number of retries before single-sector */
#define	WDIORETRIES	5	/* number of retries before giving up */
#define	RECOVERYTIME hz/2	/* time to wait before retrying a cmd */

#define	WDUNIT(dev)		DISKUNIT(dev)
#define	WDPART(dev)		DISKPART(dev)
#define WDMINOR(unit, part)     DISKMINOR(unit, part)
#define	MAKEWDDEV(maj, unit, part)	MAKEDISKDEV(maj, unit, part)

#define	WDLABELDEV(dev)	(MAKEWDDEV(major(dev), WDUNIT(dev), RAW_PART))

#define DEBUG_INTR   0x01
#define DEBUG_XFERS  0x02
#define DEBUG_STATUS 0x04
#define DEBUG_FUNCS  0x08
#define DEBUG_PROBE  0x10
#ifdef WDCDEBUG
extern int wdcdebug_wd_mask; /* init'ed in ata_wdc.c */
#define WDCDEBUG_PRINT(args, level) \
	if (wdcdebug_wd_mask & (level)) \
		printf args
#else
#define WDCDEBUG_PRINT(args, level)
#endif

struct wd_softc {
	/* General disk infos */
	struct device sc_dev;
	struct disk sc_dk;
	struct buf sc_q;
	/* IDE disk soft states */
	struct ata_bio sc_wdc_bio; /* current transfer */
	struct buf *sc_bp; /* buf being transferred */
	struct ata_drive_datas *drvp; /* Our controller's infos */
	int openings;
	struct ataparams sc_params;/* drive characteistics found */
	int sc_flags;	  
#define WDF_LOCKED	  0x01
#define WDF_WANTED	  0x02
#define WDF_WLABEL	  0x04 /* label is writable */
#define WDF_LABELLING   0x08 /* writing label */
/*
 * XXX Nothing resets this yet, but disk change sensing will when ATA-4 is
 * more fully implemented.
 */
#define WDF_LOADED	  0x10 /* parameters loaded */
#define WDF_WAIT	0x20 /* waiting for resources */
#define WDF_LBA	 0x40 /* using LBA mode */

	int sc_capacity;
	int cyl; /* actual drive parameters */
	int heads;
	int sectors;
	int retries; /* number of xfer retry */
#if NRND > 0
	rndsource_element_t	rnd_source;
#endif
	struct timeout sc_restart_timeout;
	void *sc_sdhook;
};

#define sc_drive sc_wdc_bio.drive
#define sc_mode sc_wdc_bio.mode
#define sc_multi sc_wdc_bio.multi
#define sc_badsect sc_wdc_bio.badsect

#ifndef __OpenBSD__
int	wdprobe		__P((struct device *, struct cfdata *, void *));
#else
int	wdprobe		__P((struct device *, void *, void *));
#endif
void	wdattach	__P((struct device *, struct device *, void *));
int     wddetach __P((struct device *, int));
int     wdactivate __P((struct device *, enum devact));
void    wdzeroref __P((struct device *));
int	wdprint	__P((void *, char *));

struct cfattach wd_ca = {
	sizeof(struct wd_softc), wdprobe, wdattach,
	wddetach, wdactivate, wdzeroref
};

#ifdef __OpenBSD__
struct cfdriver wd_cd = {
	NULL, "wd", DV_DISK
};
#else
extern struct cfdriver wd_cd;
#endif

void  wdgetdefaultlabel __P((struct wd_softc *, struct disklabel *));
void  wdgetdisklabel __P((dev_t dev, struct wd_softc *, 
				 struct disklabel *,
				 struct cpu_disklabel *, int));
void  wdstrategy	__P((struct buf *));
void  wdstart	__P((void *));
void  __wdstart	__P((struct wd_softc*, struct buf *));
void  wdrestart __P((void*));
int   wd_get_params __P((struct wd_softc *, u_int8_t, struct ataparams *));
void  wd_flushcache __P((struct wd_softc *, int));
void  wd_shutdown __P((void*));

struct dkdriver wddkdriver = { wdstrategy };

/* XXX: these should go elsewhere */
cdev_decl(wd);
bdev_decl(wd);

#ifdef DKBAD
void	bad144intern __P((struct wd_softc *));
#endif

#define wdlock(wd)  disk_lock(&(wd)->sc_dk)
#define wdunlock(wd)  disk_unlock(&(wd)->sc_dk)
#define wdlookup(unit) (struct wd_softc *)device_lookup(&wd_cd, (unit))


int
wdprobe(parent, match_, aux)
	struct device *parent;
#ifndef __OpenBSD__
	struct cfdata *match;
#else
	void *match_;
#endif
	void *aux;
{
	struct ata_atapi_attach *aa_link = aux;
	struct cfdata *match = match_;

	if (aa_link == NULL)
		return 0;
	if (aa_link->aa_type != T_ATA)
		return 0;

#ifndef __OpenBSD__
	if (match->cf_loc[ATACF_CHANNEL] != ATACF_CHANNEL_DEFAULT &&
	    match->cf_loc[ATACF_CHANNEL] != aa_link->aa_channel)
		return 0;

	if (match->cf_loc[ATACF_DRIVE] != ATACF_DRIVE_DEFAULT &&
	    match->cf_loc[ATACF_DRIVE] != aa_link->aa_drv_data->drive)
		return 0;
#else
	if (match->cf_loc[0] != -1 &&
	    match->cf_loc[0] != aa_link->aa_channel)
		return 0;

	if (match->cf_loc[1] != -1 &&
	    match->cf_loc[1] != aa_link->aa_drv_data->drive)
		return 0;
#endif

	return 1;
}

void
wdattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct wd_softc *wd = (void *)self;
	struct ata_atapi_attach *aa_link= aux;
	int i, blank;
	char buf[41], c, *p, *q;
	WDCDEBUG_PRINT(("wdattach\n"), DEBUG_FUNCS | DEBUG_PROBE);

	wd->openings = aa_link->aa_openings;
	wd->drvp = aa_link->aa_drv_data;

	strncpy(wd->drvp->drive_name, wd->sc_dev.dv_xname, 
		sizeof(wd->drvp->drive_name) - 1);
	wd->drvp->cf_flags = wd->sc_dev.dv_cfdata->cf_flags;

	/* read our drive info */
	if (wd_get_params(wd, at_poll, &wd->sc_params) != 0) {
		printf("%s: IDENTIFY failed\n", wd->sc_dev.dv_xname);
		return;
	}

	for (blank = 0, p = wd->sc_params.atap_model, q = buf, i = 0;
	    i < sizeof(wd->sc_params.atap_model); i++) {
		c = *p++;
		if (c == '\0')
			break;
		if (c != ' ') {
			if (blank) {
				*q++ = ' ';
				blank = 0;
			}
			*q++ = c;
		} else
			blank = 1;
		}
	*q++ = '\0';

	printf(": <%s>\n", buf);

	wdc_probe_caps(wd->drvp, &wd->sc_params);
	wdc_print_caps(wd->drvp);

	if ((wd->sc_params.atap_multi & 0xff) > 1) {
		wd->sc_multi = wd->sc_params.atap_multi & 0xff;
	} else {
		wd->sc_multi = 1;
	}

	printf("%s: %d-sector PIO,", wd->sc_dev.dv_xname, wd->sc_multi);

	/* Prior to ATA-4, LBA was optional. */
	if ((wd->sc_params.atap_capabilities1 & WDC_CAP_LBA) != 0)
		wd->sc_flags |= WDF_LBA;
#if 0
	/* ATA-4 requires LBA. */
	if (wd->sc_params.atap_ataversion != 0xffff &&
	    wd->sc_params.atap_ataversion >= WDC_VER_ATA4)
		wd->sc_flags |= WDF_LBA;
#endif

	if ((wd->sc_flags & WDF_LBA) != 0) {
		wd->sc_capacity =
		    (wd->sc_params.atap_capacity[1] << 16) |
		    wd->sc_params.atap_capacity[0];
		printf(" LBA, %dMB, %d cyl, %d head, %d sec, %d sectors\n",
		    wd->sc_capacity / (1048576 / DEV_BSIZE),
		    wd->sc_params.atap_cylinders,
		    wd->sc_params.atap_heads,
		    wd->sc_params.atap_sectors,
		    wd->sc_capacity);
	} else {
		wd->sc_capacity =
		    wd->sc_params.atap_cylinders *
		    wd->sc_params.atap_heads *
		    wd->sc_params.atap_sectors;
		printf(" CHS, %dMB, %d cyl, %d head, %d sec, %d sectors\n",
		    wd->sc_capacity / (1048576 / DEV_BSIZE),
		    wd->sc_params.atap_cylinders,
		    wd->sc_params.atap_heads,
		    wd->sc_params.atap_sectors,
		    wd->sc_capacity);
	}
	WDCDEBUG_PRINT(("%s: atap_dmatiming_mimi=%d, atap_dmatiming_recom=%d\n",
	    self->dv_xname, wd->sc_params.atap_dmatiming_mimi,
	    wd->sc_params.atap_dmatiming_recom), DEBUG_PROBE);
	/*
	 * Initialize and attach the disk structure.
	 */
	wd->sc_dk.dk_driver = &wddkdriver;
	wd->sc_dk.dk_name = wd->sc_dev.dv_xname;
	disk_attach(&wd->sc_dk);
	wd->sc_wdc_bio.lp = wd->sc_dk.dk_label;
	wd->sc_sdhook = shutdownhook_establish(wd_shutdown, wd);
	if (wd->sc_sdhook == NULL)
		printf("%s: WARNING: unable to establish shutdown hook\n",
		    wd->sc_dev.dv_xname); 
#if NRND > 0
	rnd_attach_source(&wd->rnd_source, wd->sc_dev.dv_xname,
			  RND_TYPE_DISK, 0);
#endif
	timeout_set(&wd->sc_restart_timeout, wdrestart, wd);
}

int
wdactivate(self, act)
        struct device *self;
        enum devact act;
{
        int rv = 0;

        switch (act) {
        case DVACT_ACTIVATE:
                break;

        case DVACT_DEACTIVATE:
                /*
                 * Nothing to do; we key off the device's DVF_ACTIVATE.
                 */
                break;
        }
        return (rv);
}


int
wddetach(self, flags)
        struct device *self;
        int flags;
{
        struct wd_softc *sc = (struct wd_softc *)self;
        struct buf *dp, *bp;
        int s, bmaj, cmaj, mn;

	/* Remove unprocessed buffers from queue */
	s = splbio();
	for (dp = &sc->sc_q; (bp = dp->b_actf) != NULL; ) {
		dp->b_actf = bp->b_actf;
		
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		biodone(bp);
	}
	splx(s);

        /* locate the major number */
        mn = WDMINOR(self->dv_unit, 0);

        for (bmaj = 0; bmaj < nblkdev; bmaj++)
                if (bdevsw[bmaj].d_open == wdopen)
			vdevgone(bmaj, mn, mn + MAXPARTITIONS - 1, VBLK);
        for (cmaj = 0; cmaj < nchrdev; cmaj++)
                if (cdevsw[cmaj].d_open == wdopen)
			vdevgone(cmaj, mn, mn + MAXPARTITIONS - 1, VCHR);

	/* Get rid of the shutdown hook. */
	if (sc->sc_sdhook != NULL)
		shutdownhook_disestablish(sc->sc_sdhook);

#if NRND > 0
        /* Unhook the entropy source. */
        rnd_detach_source(&sc->rnd_source);
#endif

        return (0);
}

void
wdzeroref(self)
        struct device *self;
{
        struct wd_softc *sc = (struct wd_softc *)self;

        /* Detach disk. */
        disk_detach(&sc->sc_dk);
}

/*
 * Read/write routine for a buffer.  Validates the arguments and schedules the
 * transfer.  Does not wait for the transfer to complete.
 */
void
wdstrategy(bp)
	struct buf *bp;
{
	struct wd_softc *wd;
	int s;

	wd = wdlookup(WDUNIT(bp->b_dev));
	if (wd == NULL) {
		bp->b_error = ENXIO;
		goto bad;
	}

	WDCDEBUG_PRINT(("wdstrategy (%s)\n", wd->sc_dev.dv_xname),
	    DEBUG_XFERS);
	
	/* Valid request?  */
	if (bp->b_blkno < 0 ||
	    (bp->b_bcount % wd->sc_dk.dk_label->d_secsize) != 0 ||
	    (bp->b_bcount / wd->sc_dk.dk_label->d_secsize) >= (1 << NBBY)) {
		bp->b_error = EINVAL;
		goto bad;
	}
	
	/* If device invalidated (e.g. media change, door open), error. */
	if ((wd->sc_flags & WDF_LOADED) == 0) {
		bp->b_error = EIO;
		goto bad;
	}

	/* If it's a null transfer, return immediately. */
	if (bp->b_bcount == 0)
		goto done;

	/*
	 * Do bounds checking, adjust transfer. if error, process.
	 * If end of partition, just return.
	 */
	if (WDPART(bp->b_dev) != RAW_PART &&
	    bounds_check_with_label(bp, wd->sc_dk.dk_label, wd->sc_dk.dk_cpulabel,
	    (wd->sc_flags & (WDF_WLABEL|WDF_LABELLING)) != 0) <= 0)
		goto done;
	/* Queue transfer on drive, activate drive and controller if idle. */
	s = splbio();
	disksort(&wd->sc_q, bp);
	wdstart(wd);
	splx(s);
	device_unref(&wd->sc_dev);
	return;
bad:
	bp->b_flags |= B_ERROR;
done:
	/* Toss transfer; we're done early. */
	bp->b_resid = bp->b_bcount;
	biodone(bp);
	if (wd != NULL)
		device_unref(&wd->sc_dev);
}

/*
 * Queue a drive for I/O.
 */
void
wdstart(arg)
	void *arg;
{
	struct wd_softc *wd = arg;
	struct buf *dp, *bp=0;

	WDCDEBUG_PRINT(("wdstart %s\n", wd->sc_dev.dv_xname),
	    DEBUG_XFERS);
	while (wd->openings > 0) {

		/* Is there a buf for us ? */
		dp = &wd->sc_q;
		if ((bp = dp->b_actf) == NULL)  /* yes, an assign */
			 return;
		dp->b_actf = bp->b_actf;
	
		/* 
		 * Make the command. First lock the device
		 */
		wd->openings--;

		wd->retries = 0;
		__wdstart(wd, bp);
	}
}

void
__wdstart(wd, bp)
	struct wd_softc *wd;
	struct buf *bp;
{
	daddr_t p_offset;
	if (WDPART(bp->b_dev) != RAW_PART)
		p_offset =
		    wd->sc_dk.dk_label->d_partitions[WDPART(bp->b_dev)].p_offset;
	else
		p_offset = 0;
	wd->sc_wdc_bio.blkno = bp->b_blkno + p_offset;
	wd->sc_wdc_bio.blkno /= (wd->sc_dk.dk_label->d_secsize / DEV_BSIZE);
	wd->sc_wdc_bio.blkdone =0;
	wd->sc_bp = bp;
	/*
	 * If we're retrying, retry in single-sector mode. This will give us
	 * the sector number of the problem, and will eventually allow the
	 * transfer to succeed.
	 */
	if (wd->sc_multi == 1 || wd->retries >= WDIORETRIES_SINGLE)
		wd->sc_wdc_bio.flags = ATA_SINGLE;
	else
		wd->sc_wdc_bio.flags = 0;
	if (wd->sc_flags & WDF_LBA)
		wd->sc_wdc_bio.flags |= ATA_LBA;
	if (bp->b_flags & B_READ)
		wd->sc_wdc_bio.flags |= ATA_READ;
	wd->sc_wdc_bio.bcount = bp->b_bcount;
	wd->sc_wdc_bio.databuf = bp->b_data;
	wd->sc_wdc_bio.wd = wd;
	/* Instrumentation. */
	disk_busy(&wd->sc_dk);
	switch (wdc_ata_bio(wd->drvp, &wd->sc_wdc_bio)) {
	case WDC_TRY_AGAIN:
		timeout_add(&wd->sc_restart_timeout, hz);
		break;
	case WDC_QUEUED:
		break;
	case WDC_COMPLETE:
		wddone(wd);
		break;
	default:
		panic("__wdstart: bad return code from wdc_ata_bio()");
	}
}

void
wddone(v)
	void *v;
{
	struct wd_softc *wd = v;
	struct buf *bp = wd->sc_bp;
	char buf[256], *errbuf = buf;
	WDCDEBUG_PRINT(("wddone %s\n", wd->sc_dev.dv_xname),
	    DEBUG_XFERS);

	bp->b_resid = wd->sc_wdc_bio.bcount;
	errbuf[0] = '\0';
	switch (wd->sc_wdc_bio.error) {
	case ERR_NODEV:
		bp->b_flags |= B_ERROR;
		bp->b_error = ENXIO;
		break;
	case ERR_DMA:
		errbuf = "DMA error";
		goto retry;
	case ERR_DF:
		errbuf = "device fault";
		goto retry;
	case TIMEOUT:
		errbuf = "device timeout";
		goto retry;
	case ERROR:
		/* Don't care about media change bits */
		if (wd->sc_wdc_bio.r_error != 0 &&
		    (wd->sc_wdc_bio.r_error & ~(WDCE_MC | WDCE_MCR)) == 0)
			goto noerror;
		ata_perror(wd->drvp, wd->sc_wdc_bio.r_error, errbuf);
retry:
		/* Just reset and retry. Can we do more ? */
		wdc_reset_channel(wd->drvp);
		diskerr(bp, "wd", errbuf, LOG_PRINTF,
		    wd->sc_wdc_bio.blkdone, wd->sc_dk.dk_label);
		if (wd->retries++ < WDIORETRIES) {
			printf(", retrying\n");
			timeout_add(&wd->sc_restart_timeout, RECOVERYTIME);
			return;
		}
		printf("\n");
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		break;
	case NOERROR:
noerror:	if ((wd->sc_wdc_bio.flags & ATA_CORR) || wd->retries > 0)
			printf("%s: soft error (corrected)\n",
			    wd->sc_dev.dv_xname);
	}
	disk_unbusy(&wd->sc_dk, (bp->b_bcount - bp->b_resid));
#if NRND > 0
	rnd_add_uint32(&wd->rnd_source, bp->b_blkno);
#endif
	biodone(bp);
	wd->openings++;
	wdstart(wd);
}

void
wdrestart(v)
	void *v;
{
	struct wd_softc *wd = v;
	struct buf *bp = wd->sc_bp;
	int s;
	WDCDEBUG_PRINT(("wdrestart %s\n", wd->sc_dev.dv_xname),
	    DEBUG_XFERS);

	s = splbio();
	__wdstart(v, bp);
	splx(s);
}

int
wdread(dev, uio, flags)
	dev_t dev;
	struct uio *uio;
	int flags;
{

	WDCDEBUG_PRINT(("wdread\n"), DEBUG_XFERS);
	return (physio(wdstrategy, NULL, dev, B_READ, minphys, uio));
}

int
wdwrite(dev, uio, flags)
	dev_t dev;
	struct uio *uio;
	int flags;
{

	WDCDEBUG_PRINT(("wdwrite\n"), DEBUG_XFERS);
	return (physio(wdstrategy, NULL, dev, B_WRITE, minphys, uio));
}

int
wdopen(dev, flag, fmt, p)
	dev_t dev;
	int flag, fmt;
	struct proc *p;
{
	struct wd_softc *wd;
	int unit, part;
	int error;

	WDCDEBUG_PRINT(("wdopen\n"), DEBUG_FUNCS);

	unit = WDUNIT(dev);
	wd = wdlookup(unit);
	if (wd == NULL)
		return ENXIO;

	/*
	 * If this is the first open of this device, add a reference
	 * to the adapter.
	 */
#ifndef __OpenBSD__
	if (wd->sc_dk.dk_openmask == 0 &&
	    (error = wdc_ata_addref(wd->drvp)) != 0)
		return (error);
#endif

	if ((error = wdlock(wd)) != 0)
		goto bad4;

	if (wd->sc_dk.dk_openmask != 0) {
		/*
		 * If any partition is open, but the disk has been invalidated,
		 * disallow further opens.
		 */
		if ((wd->sc_flags & WDF_LOADED) == 0) {
			error = EIO;
			goto bad3;
		}
	} else {
		if ((wd->sc_flags & WDF_LOADED) == 0) {
			wd->sc_flags |= WDF_LOADED;

			/* Load the physical device parameters. */
			wd_get_params(wd, AT_WAIT, &wd->sc_params);

			/* Load the partition info if not already loaded. */
			wdgetdisklabel(dev, wd, wd->sc_dk.dk_label,
				       wd->sc_dk.dk_cpulabel, 0);
		}
	}

	part = WDPART(dev);

	/* Check that the partition exists. */
	if (part != RAW_PART &&
	    (part >= wd->sc_dk.dk_label->d_npartitions ||
	     wd->sc_dk.dk_label->d_partitions[part].p_fstype == FS_UNUSED)) {
		error = ENXIO;
		goto bad;
	}
	
	/* Insure only one open at a time. */
	switch (fmt) {
	case S_IFCHR:
		wd->sc_dk.dk_copenmask |= (1 << part);
		break;
	case S_IFBLK:
		wd->sc_dk.dk_bopenmask |= (1 << part);
		break;
	}
	wd->sc_dk.dk_openmask =
	    wd->sc_dk.dk_copenmask | wd->sc_dk.dk_bopenmask;

	wdunlock(wd);
	device_unref(&wd->sc_dev);
	return 0;

bad:
	if (wd->sc_dk.dk_openmask == 0) {
	}

bad3:
	wdunlock(wd);
bad4:
#ifndef __OpenBSD__
	if (wd->sc_dk.dk_openmask == 0)
		wdc_ata_delref(wd->drvp);
#endif
	device_unref(&wd->sc_dev);
	return error;
}

int
wdclose(dev, flag, fmt, p)
	dev_t dev;
	int flag, fmt;
	struct proc *p;
{
	struct wd_softc *wd;
	int part = WDPART(dev);
	int error = 0;

	wd = wdlookup(WDUNIT(dev));
	if (wd == NULL)
		return ENXIO;
	
	WDCDEBUG_PRINT(("wdclose\n"), DEBUG_FUNCS);
	if ((error = wdlock(wd)) != 0)
		goto exit;

	switch (fmt) {
	case S_IFCHR:
		wd->sc_dk.dk_copenmask &= ~(1 << part);
		break;
	case S_IFBLK:
		wd->sc_dk.dk_bopenmask &= ~(1 << part);
		break;
	}
	wd->sc_dk.dk_openmask =
	    wd->sc_dk.dk_copenmask | wd->sc_dk.dk_bopenmask;

	if (wd->sc_dk.dk_openmask == 0) {
		wd_flushcache(wd,0);
		/* XXXX Must wait for I/O to complete! */
#ifndef __OpenBSD__
		wdc_ata_delref(wd->drvp);
#endif
	}

	wdunlock(wd);

 exit:
	device_unref(&wd->sc_dev);
	return (error);
}

void
wdgetdefaultlabel(wd, lp)
	struct wd_softc *wd;
	struct disklabel *lp;
{
	WDCDEBUG_PRINT(("wdgetdefaultlabel\n"), DEBUG_FUNCS);
	bzero(lp, sizeof(struct disklabel));

	lp->d_secsize = DEV_BSIZE;
	lp->d_ntracks = wd->sc_params.atap_heads;
	lp->d_nsectors = wd->sc_params.atap_sectors;
	lp->d_ncylinders = wd->sc_params.atap_cylinders;
	lp->d_secpercyl = lp->d_ntracks * lp->d_nsectors;
	if (wd->drvp->ata_vers == -1) {
		lp->d_type = DTYPE_ST506;
		strncpy(lp->d_typename, "ST506/MFM/RLL", 16);
	} else {
		lp->d_type = DTYPE_ESDI;
		strncpy(lp->d_typename, "ESDI/IDE disk", 16);
	}
	/* XXX - user viscopy() like sd.c */
	strncpy(lp->d_packname, wd->sc_params.atap_model, 16);
	lp->d_secperunit = wd->sc_capacity;
	lp->d_rpm = 3600;
	lp->d_interleave = 1;
	lp->d_flags = 0;

	lp->d_partitions[RAW_PART].p_offset = 0;
	lp->d_partitions[RAW_PART].p_size =
	lp->d_secperunit * (lp->d_secsize / DEV_BSIZE);
	lp->d_partitions[RAW_PART].p_fstype = FS_UNUSED;
	lp->d_npartitions = RAW_PART + 1;

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = dkcksum(lp);
}

/*
 * Fabricate a default disk label, and try to read the correct one.
 */
void
wdgetdisklabel(dev, wd, lp, clp, spoofonly)
	dev_t  dev;
	struct wd_softc *wd;
	struct disklabel *lp;
	struct cpu_disklabel *clp;
	int spoofonly;
{
	char *errstring;

	WDCDEBUG_PRINT(("wdgetdisklabel\n"), DEBUG_FUNCS);

	bzero(clp, sizeof(struct cpu_disklabel));

	wdgetdefaultlabel(wd, lp);

	wd->sc_badsect[0] = -1;

	if (wd->drvp->state > RECAL)
		wd->drvp->drive_flags |= DRIVE_RESET;
	errstring = readdisklabel(WDLABELDEV(dev),
	    wdstrategy, lp, clp, spoofonly);
	if (errstring) {
		/*
		 * This probably happened because the drive's default
		 * geometry doesn't match the DOS geometry.  We
		 * assume the DOS geometry is now in the label and try
		 * again.  XXX This is a kluge.
		 */
		if (wd->drvp->state > RECAL)
			wd->drvp->drive_flags |= DRIVE_RESET;
		errstring = readdisklabel(WDLABELDEV(dev),
		    wdstrategy, lp, clp, spoofonly);
	}
	if (errstring) {
		printf("%s: %s\n", wd->sc_dev.dv_xname, errstring);
		return;
	}

	if (wd->drvp->state > RECAL)
		wd->drvp->drive_flags |= DRIVE_RESET;
#ifdef DKBAD
	if ((lp->d_flags & D_BADSECT) != 0)
		bad144intern(wd);
#endif
}

int
wdioctl(dev, xfer, addr, flag, p)
	dev_t dev;
	u_long xfer;
	caddr_t addr;
	int flag;
	struct proc *p;
{
	struct wd_softc *wd;
	int error = 0;

	WDCDEBUG_PRINT(("wdioctl\n"), DEBUG_FUNCS);

	wd = wdlookup(WDUNIT(dev));
	if (wd == NULL)
		return ENXIO;
	
	if ((wd->sc_flags & WDF_LOADED) == 0) {
		error = EIO;
		goto exit;
	}

	switch (xfer) {
#ifdef DKBAD
	case DIOCSBAD:
		if ((flag & FWRITE) == 0)
			return EBADF;
		DKBAD(wd->sc_dk.dk_cpulabel) = *(struct dkbad *)addr;
		wd->sc_dk.dk_label->d_flags |= D_BADSECT;
		bad144intern(wd);
		goto exit;
#endif

	case DIOCRLDINFO:
		wdgetdisklabel(dev, wd, wd->sc_dk.dk_label,
		    wd->sc_dk.dk_cpulabel, 0);
		goto exit;
	case DIOCGPDINFO: {
			struct cpu_disklabel osdep;

			wdgetdisklabel(dev, wd, (struct disklabel *)addr,
			    &osdep, 1);
			goto exit;
		}

	case DIOCGDINFO:
		*(struct disklabel *)addr = *(wd->sc_dk.dk_label);
		goto exit;
	
	case DIOCGPART:
		((struct partinfo *)addr)->disklab = wd->sc_dk.dk_label;
		((struct partinfo *)addr)->part =
		    &wd->sc_dk.dk_label->d_partitions[WDPART(dev)];
		goto exit;
	
	case DIOCWDINFO:
	case DIOCSDINFO:
		if ((flag & FWRITE) == 0) {
			error = EBADF;
			goto exit;
		}

		if ((error = wdlock(wd)) != 0)
			goto exit;
		wd->sc_flags |= WDF_LABELLING;

		error = setdisklabel(wd->sc_dk.dk_label,
		    (struct disklabel *)addr, /*wd->sc_dk.dk_openmask : */0,
		    wd->sc_dk.dk_cpulabel);
		if (error == 0) {
			if (wd->drvp->state > RECAL)
				wd->drvp->drive_flags |= DRIVE_RESET;
			if (xfer == DIOCWDINFO)
				error = writedisklabel(WDLABELDEV(dev),
				    wdstrategy, wd->sc_dk.dk_label,
				    wd->sc_dk.dk_cpulabel);
		}

		wd->sc_flags &= ~WDF_LABELLING;
		wdunlock(wd);
		goto exit;
	
	case DIOCWLABEL:
		if ((flag & FWRITE) == 0) {
			error = EBADF;
			goto exit;
		}

		if (*(int *)addr)
			wd->sc_flags |= WDF_WLABEL;
		else
			wd->sc_flags &= ~WDF_WLABEL;
		goto exit;

#ifndef __OpenBSD__
	case DIOCGDEFLABEL:
		wdgetdefaultlabel(wd, (struct disklabel *)addr);
		goto exit;
#endif

#ifdef notyet
	case DIOCWFORMAT:
		if ((flag & FWRITE) == 0)
			return EBADF;
		{
		register struct format_op *fop;
		struct iovec aiov;
		struct uio auio;
	
		fop = (struct format_op *)addr;
		aiov.iov_base = fop->df_buf;
		aiov.iov_len = fop->df_count;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_resid = fop->df_count;
		auio.uio_segflg = 0;
		auio.uio_offset =
			fop->df_startblk * wd->sc_dk.dk_label->d_secsize;
		auio.uio_procp = p;
		error = physio(wdformat, NULL, dev, B_WRITE, minphys,
		    &auio);
		fop->df_count -= auio.uio_resid;
		fop->df_reg[0] = wdc->sc_status;
		fop->df_reg[1] = wdc->sc_error;
		goto exit;
		}
#endif
		
	default:
		error = wdc_ioctl(wd->drvp, xfer, addr, flag);
		goto exit;
	}

#ifdef DIAGNOSTIC
	panic("wdioctl: impossible");
#endif

 exit:
	device_unref(&wd->sc_dev);
	return (error);
}

#ifdef B_FORMAT
int
wdformat(struct buf *bp)
{

	bp->b_flags |= B_FORMAT;
	return wdstrategy(bp);
}
#endif

int
wdsize(dev)
	dev_t dev;
{
	struct wd_softc *wd;
	int part, omask;
	int size;

	WDCDEBUG_PRINT(("wdsize\n"), DEBUG_FUNCS);

	wd = wdlookup(WDUNIT(dev));
	if (wd == NULL)
		return (-1);

	part = WDPART(dev);
	omask = wd->sc_dk.dk_openmask & (1 << part);

	if (omask == 0 && wdopen(dev, 0, S_IFBLK, NULL) != 0) {
		size = -1;
		goto exit;
	}

	if (wd->sc_dk.dk_label->d_partitions[part].p_fstype != FS_SWAP)
		size = -1;
	else
		size = wd->sc_dk.dk_label->d_partitions[part].p_size *
		    (wd->sc_dk.dk_label->d_secsize / DEV_BSIZE);
	if (omask == 0 && wdclose(dev, 0, S_IFBLK, NULL) != 0)
		size = -1;

 exit:
	device_unref(&wd->sc_dev);
	return (size);
}

#ifndef __BDEVSW_DUMP_OLD_TYPE
/* #define WD_DUMP_NOT_TRUSTED if you just want to watch */
static int wddoingadump = 0;
static int wddumprecalibrated = 0;
static int wddumpmulti = 1;

/*
 * Dump core after a system crash.
 */
int
wddump(dev, blkno, va, size)
	dev_t dev;
	daddr_t blkno;
	caddr_t va;
	size_t size;
{
	struct wd_softc *wd;	/* disk unit to do the I/O */
	struct disklabel *lp;   /* disk's disklabel */
	int unit, part;
	int nblks;	/* total number of sectors left to write */
	int err;
	char errbuf[256];

	/* Check if recursive dump; if so, punt. */
	if (wddoingadump)
		return EFAULT;
	wddoingadump = 1;

	unit = WDUNIT(dev);
	wd = wdlookup(unit);
	if (wd == NULL)
		return ENXIO;

	part = WDPART(dev);

	/* Make sure it was initialized. */
	if (wd->drvp->state < READY)
		return ENXIO;

	/* Convert to disk sectors.  Request must be a multiple of size. */
	lp = wd->sc_dk.dk_label;
	if ((size % lp->d_secsize) != 0)
		return EFAULT;
	nblks = size / lp->d_secsize;
	blkno = blkno / (lp->d_secsize / DEV_BSIZE);

	/* Check transfer bounds against partition size. */
	if ((blkno < 0) || ((blkno + nblks) > lp->d_partitions[part].p_size))
		return EINVAL;  

	/* Offset block number to start of partition. */
	blkno += lp->d_partitions[part].p_offset;

	/* Recalibrate, if first dump transfer. */
	if (wddumprecalibrated == 0) {
		wddumpmulti = wd->sc_multi;
		wddumprecalibrated = 1;
		wd->drvp->state = RECAL;
	}
  
	while (nblks > 0) {
again:
		wd->sc_wdc_bio.blkno = blkno;
		wd->sc_wdc_bio.flags = ATA_POLL;
		if (wddumpmulti == 1)
			wd->sc_wdc_bio.flags |= ATA_SINGLE;
		if (wd->sc_flags & WDF_LBA)
			wd->sc_wdc_bio.flags |= ATA_LBA;
		wd->sc_wdc_bio.bcount =
			min(nblks, wddumpmulti) * lp->d_secsize;
		wd->sc_wdc_bio.databuf = va;
		wd->sc_wdc_bio.wd = wd;
#ifndef WD_DUMP_NOT_TRUSTED
		switch (wdc_ata_bio(wd->drvp, &wd->sc_wdc_bio)) {
		case WDC_TRY_AGAIN:
			panic("wddump: try again");
			break;
		case WDC_QUEUED:
			panic("wddump: polled command has been queued");
			break;
		case WDC_COMPLETE:
			break;
		}
		switch(wd->sc_wdc_bio.error) {
		case TIMEOUT:
			printf("wddump: device timed out");
			err = EIO;
			break;
		case ERR_DF:
			printf("wddump: drive fault");
			err = EIO;
			break;
		case ERR_DMA:
			printf("wddump: DMA error");
			err = EIO;
			break;
		case ERROR:
			errbuf[0] = '\0';
			ata_perror(wd->drvp, wd->sc_wdc_bio.r_error, errbuf);
			printf("wddump: %s", errbuf);
			err = EIO;
			break;
		case NOERROR: 
			err = 0;
			break;
		default:
			panic("wddump: unknown error type");
		}
		if (err != 0) {
			if (wddumpmulti != 1) {
				wddumpmulti = 1; /* retry in single-sector */
				printf(", retrying\n");
				goto again;
			}
			printf("\n");
			return err;
		}
#else	/* WD_DUMP_NOT_TRUSTED */
		/* Let's just talk about this first... */
		printf("wd%d: dump addr 0x%x, cylin %d, head %d, sector %d\n",
		    unit, va, cylin, head, sector);
		delay(500 * 1000);	/* half a second */
#endif

		/* update block count */
		nblks -= min(nblks, wddumpmulti);
		blkno += min(nblks, wddumpmulti);
		va += min(nblks, wddumpmulti) * lp->d_secsize;
	}

	wddoingadump = 0;
	return 0;
}
#else /* __BDEVSW_DUMP_NEW_TYPE */


int
wddump(dev, blkno, va, size)
	dev_t dev;
	daddr_t blkno;
	caddr_t va;
	size_t size;
{

	/* Not implemented. */
	return ENXIO;
}
#endif /* __BDEVSW_DUMP_NEW_TYPE */

#ifdef DKBAD
/*
 * Internalize the bad sector table.
 */
void
bad144intern(wd)
	struct wd_softc *wd;
{
	struct dkbad *bt = &DKBAD(wd->sc_dk.dk_cpulabel);
	struct disklabel *lp = wd->sc_dk.dk_label;
	int i = 0;

	WDCDEBUG_PRINT(("bad144intern\n"), DEBUG_XFERS);

	for (; i < NBT_BAD; i++) {
		if (bt->bt_bad[i].bt_cyl == 0xffff)
			break;
		wd->sc_badsect[i] =
		    bt->bt_bad[i].bt_cyl * lp->d_secpercyl +
		    (bt->bt_bad[i].bt_trksec >> 8) * lp->d_nsectors +
		    (bt->bt_bad[i].bt_trksec & 0xff);
	}
	for (; i < NBT_BAD+1; i++)
		wd->sc_badsect[i] = -1;
}
#endif

int
wd_get_params(wd, flags, params)
	struct wd_softc *wd;
	u_int8_t flags;
	struct ataparams *params;
{
	switch (ata_get_params(wd->drvp, flags, params)) {
	case CMD_AGAIN:
		return 1;
	case CMD_ERR:
		/*
		 * We `know' there's a drive here; just assume it's old.
		 * This geometry is only used to read the MBR and print a
		 * (false) attach message.
		 */
		strncpy(params->atap_model, "ST506",
		    sizeof params->atap_model);
		params->atap_config = ATA_CFG_FIXED;
		params->atap_cylinders = 1024;
		params->atap_heads = 8;
		params->atap_sectors = 17;
		params->atap_multi = 1;
		params->atap_capabilities1 = params->atap_capabilities2 = 0;
		wd->drvp->ata_vers = -1; /* Mark it as pre-ATA */
		return 0;
	case CMD_OK:
		return 0;
	default:
		panic("wd_get_params: bad return code from ata_get_params");
		/* NOTREACHED */
	}
}

void
wd_flushcache(wd, flags)
	struct wd_softc *wd;
	int flags;
{
	struct wdc_command wdc_c;

	if (wd->drvp->ata_vers < 4) /* WDCC_FLUSHCACHE is here since ATA-4 */
		return;
	bzero(&wdc_c, sizeof(struct wdc_command));
	wdc_c.r_command = WDCC_FLUSHCACHE;
	wdc_c.r_st_bmask = WDCS_DRDY;
	wdc_c.r_st_pmask = WDCS_DRDY;
	if (flags != 0) {
		wdc_c.flags = AT_POLL;
	} else {
		wdc_c.flags = AT_WAIT;
	}
	wdc_c.timeout = 30000; /* 30s timeout */
	if (wdc_exec_command(wd->drvp, &wdc_c) != WDC_COMPLETE) {
		printf("%s: flush cache command didn't complete\n",
		    wd->sc_dev.dv_xname);
	}
	if (wdc_c.flags & AT_TIMEOU) {
		printf("%s: flush cache command timeout\n",
		    wd->sc_dev.dv_xname);
	}
	if (wdc_c.flags & AT_DF) {
		printf("%s: flush cache command: drive fault\n",
		    wd->sc_dev.dv_xname);
	}
	/*
	 * Ignore error register, it shouldn't report anything else
	 * than COMMAND ABORTED, which means the device doesn't support
	 * flush cache
	 */
}

void
wd_shutdown(arg)
	void *arg;
{
	struct wd_softc *wd = arg;
	wd_flushcache(wd, AT_POLL);
}
