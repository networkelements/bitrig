/* $Id: cmds.c,v 1.10 2001/07/19 21:24:27 rees Exp $ */

/*
 * Smartcard commander.
 * Written by Jim Rees and others at University of Michigan.
 */

/*
copyright 2001
the regents of the university of michigan
all rights reserved

permission is granted to use, copy, create derivative works 
and redistribute this software and such derivative works 
for any purpose, so long as the name of the university of 
michigan is not used in any advertising or publicity 
pertaining to the use or distribution of this software 
without specific, written prior authorization.  if the 
above copyright notice or any other identification of the 
university of michigan is included in any copy of any 
portion of this software, then the disclaimer below must 
also be included.

this software is provided as is, without representation 
from the university of michigan as to its fitness for any 
purpose, and without warranty by the university of 
michigan of any kind, either express or implied, including 
without limitation the implied warranties of 
merchantability and fitness for a particular purpose. the 
regents of the university of michigan shall not be liable 
for any damages, including special, indirect, incidental, or 
consequential damages, with respect to any claim arising 
out of or in connection with the use of the software, even 
if it has been or is hereafter advised of the possibility of 
such damages.
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sectok.h>
#include <sc7816.h>

#include "sc.h"

#define CARDIOSIZE 200

struct {
    char *cmd, *help;
    int (*action) (int ac, char *av[]);
} dispatch_table[] = {
    /* Non-card commands */
    { "help", "[command]", help },
    { "?", "[command]", help },
    { "reset", "[ -1234ivf ]", reset },
    { "open", "[ -1234ivf ]", reset },
    { "close", "", dclose },
    { "quit", "", quit },

    /* 7816-4 commands */
    { "apdu", "[ -c class ] ins p1 p2 p3 data ...", apdu },
    { "fid", "fid", selfid },
    { "isearch", "", isearch },
    { "class", "[ class ]", class },
    { "read", "[ -x ] filesize", dread },
    { "write", "input-filename", dwrite },

    /* Cyberflex commands */
    { "ls", "[ -l ]", ls },
    { "acl", "fid [ principal: r1 r2 ... ]", acl },
    { "create", "fid size", jcreate },
    { "delete", "fid", jdelete },
    { "jdefault", "[ -d ]", jdefault },
    { "jatr", "", jatr },
    { "jdata", "", jdata },
    { "login", "[ -d ] [ -k keyno ] [ -v ] [ -x hex-aut0 ]", jlogin },
    { "jaut", "", jaut },
    { "jload", "[ -p progID ] [ -c contID ] [ -s cont_size ] [ -i inst_size ] [ -a aid ] [ -v ] filename", jload },
    { "junload", "[ -p progID ] [ -c contID ]", junload },
    { "jselect", "[ -a aid ] [ -d ]", jselect },
    { "setpass", "[ -d ] [ -x hex-aut0 ]", jsetpass },
    { NULL, NULL, NULL }
};

int dispatch(int ac, char *av[])
{
    int i;

    if (ac < 1)
	return 0;

    for (i = 0; dispatch_table[i].cmd; i++) {
	if (!strncmp(av[0], dispatch_table[i].cmd, strlen(av[0]))) {
	    (dispatch_table[i].action) (ac, av);
	    break;
	}
    }
    if (!dispatch_table[i].cmd) {
	printf("unknown command \"%s\"\n", av[0]);
	return -1;
    }
    return 0;
}

int help(int ac, char *av[])
{
    int i, j;

    if (ac < 2) {
	for (i = 0; dispatch_table[i].cmd; i++)
	    printf("%s\n", dispatch_table[i].cmd);
    } else {
	for (j = 1; j < ac; j++) {
	    for (i = 0; dispatch_table[i].cmd; i++)
		if (!strncmp(av[j], dispatch_table[i].cmd, strlen(av[0])))
		    break;
	    if (dispatch_table[i].help)
		printf("%s %s\n", dispatch_table[i].cmd, dispatch_table[i].help);
	    else
		printf("no help on \"%s\"\n", av[j]);
	}
    }

    return 0;
}

int reset(int ac, char *av[])
{
    int i, n, oflags = 0, rflags = 0, vflag = 0, sw;
    unsigned char atr[34];
    struct scparam param;

    optind = optreset = 1;

    while ((i = getopt(ac, av, "1234ivf")) != -1) {
	switch (i) {
	case '1':
	case '2':
	case '3':
	case '4':
	    port = i - '1';
	    break;
	case 'i':
	    oflags |= STONOWAIT;
	    break;
	case 'v':
	    vflag = 1;
	    break;
	case 'f':
	    rflags |= STRFORCE;
	    break;
	}
    }

    if (fd < 0) {
	fd = sectok_open(port, oflags, &sw);
	if (fd < 0) {
	    sectok_print_sw(sw);
	    return -1;
	}
    }

    aut0_vfyd = 0;

    n = sectok_reset(fd, rflags, atr, &sw);
    if (vflag)
	parse_atr(fd, SCRV, atr, n, &param);
    if (!sectok_swOK(sw)) {
	printf("sectok_reset: %s\n", sectok_get_sw(sw));
	dclose(0, NULL);
	return -1;
    }

    return 0;
}

int dclose(int ac, char *av[])
{
    if (fd >= 0) {
	scclose(fd);
	fd = -1;
    }
    return 0;
}

int quit(int ac, char *av[])
{
    exit(0);
}

int apdu(int ac, char *av[])
{
    int i, ilen, olen, n, ins, xcl = cla, p1, p2, p3, sw;
    unsigned char ibuf[256], obuf[256], *bp;

    optind = optreset = 1;

    while ((i = getopt(ac, av, "c:")) != -1) {
	switch (i) {
	case 'c':
	    sscanf(optarg, "%x", &xcl);
	    break;
	}
    }

    if (ac - optind < 4) {
	printf("usage: apdu [ -c class ] ins p1 p2 p3 data ...\n");
	return -1;
    }

    sscanf(av[optind++], "%x", &ins);
    sscanf(av[optind++], "%x", &p1);
    sscanf(av[optind++], "%x", &p2);
    sscanf(av[optind++], "%x", &p3);

    for (bp = ibuf, i = optind, ilen = 0; i < ac; i++) {
	sscanf(av[i], "%x", &n);
	*bp++ = n;
	ilen++;
    }

    if (fd < 0 && reset(0, NULL) < 0)
	return -1;

    olen = (p3 && !ilen) ? p3 : sizeof obuf;

    n = sectok_apdu(fd, xcl, ins, p1, p2, ilen, ibuf, olen, obuf, &sw);

    sectok_dump_reply(obuf, n, sw);

    return 0;
}

int selfid(int ac, char *av[])
{
    unsigned char fid[2];
    int sw;

    if (ac != 2) {
	printf("usage: f fid\n");
	return -1;
    }

    if (fd < 0 && reset(0, NULL) < 0)
	return -1;

    sectok_parse_fname(av[1], fid);
    if (sectok_selectfile(fd, cla, fid, &sw) < 0) {
	printf("selectfile: %s\n", sectok_get_sw(sw));
	return -1;
    }

    return 0;
}

int isearch(int ac, char *av[])
{
    int i, r1, r2;
    unsigned char buf[256];

    if (fd < 0 && reset(0, NULL) < 0)
	return -1;

    /* find instructions */
    for (i = 0; i < 0xff; i += 2)
	if (scread(fd, cla, i, 0, 0, 0, buf, &r1, &r2) == 0
	    && r1 != 0x6d && r1 != 0x6e)
	    printf("%02x %s %s\n", i, lookup_cmdname(i), get_r1r2s(r1, r2));
    return 0;
}

int class(int ac, char *av[])
{
    if (ac > 1)
	sscanf(av[1], "%x", &cla);
    else
	printf("Class %02x\n", cla);
    return 0;
}

int dread(int ac, char *av[])
{
    int i, n, col = 0, p3, fsize, xflag = 0, sw;
    unsigned char buf[CARDIOSIZE];

    optind = optreset = 1;

    while ((i = getopt(ac, av, "x")) != -1) {
	switch (i) {
	case 'x':
	    xflag = 1;
	    break;
	}
    }

    if (ac - optind < 1) {
	printf("usage: read [ -x ] filesize\n");
	return -1;
    }

    sscanf(av[optind++], "%d", &fsize);

    if (fd < 0 && reset(0, NULL) < 0)
	return -1;

    for (p3 = 0; fsize && p3 < 100000; p3 += n) {
	n = (fsize < CARDIOSIZE) ? fsize : CARDIOSIZE;
	n = sectok_apdu(fd, cla, 0xb0, p3 >> 8, p3 & 0xff, 0, NULL, n, buf, &sw);
	if (!sectok_swOK(sw)) {
	    printf("read binary: %s\n", sectok_get_sw(sw));
	    break;
	}
	if (xflag) {
	    for (i = 0; i < n; i++) {
		printf("%02x ", buf[i]);
		if (col++ % 16 == 15)
		    printf("\n");
	    }
	} else
	    fwrite(buf, 1, n, stdout);
	fsize -= n;
    }

    if (xflag && col % 16 != 0)
	printf("\n");

    return 0;
}

int dwrite(int ac, char *av[])
{
    int n, p3, r1, r2;
    FILE *f;
    unsigned char buf[CARDIOSIZE];

    if (ac != 2) {
	printf("usage: write input-filename\n");
	return -1;
    }

    if (fd < 0 && reset(0, NULL) < 0)
	return -1;

    f = fopen(av[1], "r");
    if (!f) {
	printf("can't open %s\n", av[1]);
	return -1;
    }

    n = 0;
    while ((p3 = fread(buf, 1, CARDIOSIZE, f)) > 0) {
	if (scwrite(fd, cla, 0xd6, n >> 8, n & 0xff, p3, buf, &r1, &r2) < 0) {
	    printf("scwrite failed\n");
	    break;
	}
	if (r1 != 0x90 && r1 != 0x61) {
	    print_r1r2(r1, r2);
	    break;
	}
	n += p3;
    }
    fclose(f);

    return (n ? 0 : -1);
}
