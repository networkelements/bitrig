#	$OpenBSD: Makefile,v 1.388 2014/07/23 11:49:06 reyk Exp $

TZDIR=		/usr/share/zoneinfo
LOCALTIME=	Canada/Mountain
MTREEDIR=	/etc/mtree

NOOBJ=

KERNELS = GENERIC bsd

.if exists(etc.${MACHINE}/Makefile.inc)
.include "etc.${MACHINE}/Makefile.inc"
.endif

.for CONF K in ${KERNELS}
.  if !target($K)
$K:
	cd ../sys/arch/${MACHINE}/conf && config ${CONF}
	cd ../sys/arch/${MACHINE}/compile/${CONF} && \
	    ${MAKE} clean && exec ${MAKE}
.  endif
ALL_KERNELS += $K
.endfor

kernels: bootblocks ${ALL_KERNELS}
.for CONF K in ${KERNELS}
	cp ../sys/arch/${MACHINE}/compile/${CONF}/bsd ${RELEASEDIR}/$K
.endfor

# target for MD to override for post-kernel procssing.
post-kernels:
	
# -rw-r--r--
BINOWN= root
BINGRP= wheel
MUTABLE=changelist csh.cshrc csh.login csh.logout daily dhclient.conf \
	etc.${MACHINE}/disktab etc.${MACHINE}/login.conf ftpusers \
	gettytab group hosts ksh.kshrc locate.rc mailer.conf man.conf \
	moduli monthly myname netstart networks newsyslog.conf \
	pf.os protocols rc rc.conf rpc services shells syslog.conf weekly

# -rw-r--r--
EXAMPLES=chio.conf dhcpd.conf exports ftpchroot hosts.lpd httpd.conf \
	ifstated.conf inetd.conf mixerctl.conf mrouted.conf ntpd.conf \
	printcap rbootd.conf remote sensorsd.conf
.if ${MACHINE} != "aviion" 
EXAMPLES+= wsconsctl.conf
.endif

# -rw-------
EXAMPLES_600=bgpd.conf dvmrpd.conf hostapd.conf iked.conf ipsec.conf \
	ldapd.conf ldpd.conf ospf6d.conf ospfd.conf pf.conf rc.local \
	rc.securelevel rc.shutdown relayd.conf ripd.conf \
	sasyncd.conf snmpd.conf ypldap.conf 

# -r-xr-xr-x
RCDAEMONS=	apmd bgpd bootparamd cron dhcpd dhcrelay dvmrpd \
		ftpd ftpproxy hostapd hotplugd httpd identd ifstated iked \
		inetd isakmpd ldapd npppd ldattach ldpd lpd mrouted \
		named nginx nsd ntpd ospfd ospf6d portmap pflogd rarpd rbootd \
		relayd ripd route6d rtadvd rtsold sasyncd \
		sensorsd slowcgi smtpd snmpd spamd sshd syslogd watchdogd \
		wsmoused xdm ypbind ypldap yppasswdd ypserv nfsd mountd lockd \
		statd spamlogd sndiod tftpd tftpproxy unbound iscsid

MISETS=	base${OSrev}.tgz comp${OSrev}.tgz etc${OSrev}.tgz game${OSrev}.tgz

all clean cleandir depend etc install lint:

install-mtree:
	${INSTALL} -c -o root -g wheel -m 600 mtree/special \
	    ${DESTDIR}${MTREEDIR}
	${INSTALL} -c -o root -g wheel -m 444 mtree/4.4BSD.dist \
	    ${DESTDIR}${MTREEDIR}
	${INSTALL} -c -o root -g wheel -m 444 mtree/BSD.x11.dist \
	    ${DESTDIR}${MTREEDIR}

.ifndef DESTDIR
distribution-etc-root-var distribution distrib-dirs release:
	@echo setenv DESTDIR before doing that!
	@false
.else
distribution-etc-root-var: distrib-dirs
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 ${MUTABLE} ${DESTDIR}/etc
	sh ttys.pty | cat etc.${MACHINE}/ttys - > ${DESTDIR}/etc/ttys && \
	    chown ${BINOWN} ${DESTDIR}/etc/ttys && \
	    chgrp ${BINGRP} ${DESTDIR}/etc/ttys && \
	    chmod 644 ${DESTDIR}/etc/ttys
	cat examples/sysctl.conf etc.${MACHINE}/sysctl.conf > \
	    ${DESTDIR}/etc/examples/sysctl.conf && \
	    chown ${BINOWN} ${DESTDIR}/etc/examples/sysctl.conf && \
	    chgrp ${BINGRP} ${DESTDIR}/etc/examples/sysctl.conf && \
	    chmod 644 ${DESTDIR}/etc/examples/sysctl.conf
	cat fbtab.head etc.${MACHINE}/fbtab fbtab.tail > ${DESTDIR}/etc/fbtab && \
	    chown ${BINOWN} ${DESTDIR}/etc/fbtab && \
	    chgrp ${BINGRP} ${DESTDIR}/etc/fbtab && \
	    chmod 644 ${DESTDIR}/etc/fbtab
	${INSTALL} -c -o ${BINOWN} -g operator -m 664 motd ${DESTDIR}/etc
	${INSTALL} -c -o root -g crontab -m 600 crontab ${DESTDIR}/var/cron/tabs/root
	${INSTALL} -c -o root -g wheel -m 600 master.passwd ${DESTDIR}/etc
	pwd_mkdb -p -d ${DESTDIR}/etc /etc/master.passwd
	${INSTALL} -c -o root -g wheel -m 600 pf.conf ${DESTDIR}/etc
	${INSTALL} -c -o root -g _nsd -m 640 nsd.conf ${DESTDIR}/var/nsd/etc
	${INSTALL} -c -o root -g wheel -m 644 unbound.conf ${DESTDIR}/var/unbound/etc
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 555 \
	    etc.${MACHINE}/MAKEDEV ${DESTDIR}/dev
	cd root; \
		${INSTALL} -c -o root -g wheel -m 644 dot.cshrc \
		    ${DESTDIR}/root/.cshrc; \
		${INSTALL} -c -o root -g wheel -m 644 dot.login \
		    ${DESTDIR}/root/.login; \
		${INSTALL} -c -o root -g wheel -m 644 dot.profile \
		    ${DESTDIR}/root/.profile; \
		${INSTALL} -c -o root -g wheel -m 644 dot.Xdefaults \
		    ${DESTDIR}/root/.Xdefaults; \
		rm -f ${DESTDIR}/.cshrc ${DESTDIR}/.profile; \
		${INSTALL} -c -o root -g wheel -m 644 dot.cshrc \
		    ${DESTDIR}/.cshrc; \
		${INSTALL} -c -o root -g wheel -m 644 dot.profile \
		    ${DESTDIR}/.profile
	cd skel; \
		${INSTALL} -c -o root -g wheel -m 644 dot.cshrc \
		    ${DESTDIR}/etc/skel/.cshrc; \
		${INSTALL} -c -o root -g wheel -m 644 dot.login \
		    ${DESTDIR}/etc/skel/.login; \
		${INSTALL} -c -o root -g wheel -m 644 dot.mailrc \
		    ${DESTDIR}/etc/skel/.mailrc; \
		${INSTALL} -c -o root -g wheel -m 644 dot.profile \
		    ${DESTDIR}/etc/skel/.profile; \
		${INSTALL} -c -o root -g wheel -m 644 dot.Xdefaults \
		    ${DESTDIR}/etc/skel/.Xdefaults; \
		${INSTALL} -c -o root -g wheel -m 600 /dev/null \
		    ${DESTDIR}/etc/skel/.ssh/authorized_keys
	cd ppp; \
		${INSTALL} -c -o root -g wheel -m 600 chap-secrets \
		    ${DESTDIR}/etc/ppp; \
		${INSTALL} -c -o root -g wheel -m 600 options \
		    ${DESTDIR}/etc/ppp; \
		${INSTALL} -c -o root -g wheel -m 600 options.sample \
		    ${DESTDIR}/etc/ppp; \
		${INSTALL} -c -o root -g wheel -m 600 chatscript.sample \
		    ${DESTDIR}/etc/ppp; \
		${INSTALL} -c -o root -g wheel -m 600 pap-secrets \
		    ${DESTDIR}/etc/ppp
	cd examples; \
		${INSTALL} -c -o root -g wheel -m 644 ${EXAMPLES} \
		    ${DESTDIR}/etc/examples; \
		${INSTALL} -c -o root -g wheel -m 600 ${EXAMPLES_600} \
		    ${DESTDIR}/etc/examples
	cd signify; \
		${INSTALL} -c -o root -g wheel -m 644 *.pub \
		    ${DESTDIR}/etc/signify
	cd systrace; \
		${INSTALL} -c -o root -g wheel -m 600 usr_sbin_lpd \
		    ${DESTDIR}/etc/systrace; \
		${INSTALL} -c -o root -g wheel -m 600 usr_sbin_named \
		    ${DESTDIR}/etc/systrace
	cd bind; \
		${INSTALL} -c -o root -g named -m 640 named-simple.conf \
		    ${DESTDIR}/var/named/etc/named.conf; \
		${INSTALL} -c -o root -g named -m 640 named-*.conf \
		    ${DESTDIR}/var/named/etc; \
		${INSTALL} -c -o root -g wheel -m 644 root.hint \
		    ${DESTDIR}/var/named/etc; \
		${INSTALL} -c -o root -g wheel -m 644 db.localhost \
		    ${DESTDIR}/var/named/standard/localhost; \
		${INSTALL} -c -o root -g wheel -m 644 db.loopback \
		    ${DESTDIR}/var/named/standard/loopback; \
		${INSTALL} -c -o root -g wheel -m 644 db.loopback6.arpa \
		    ${DESTDIR}/var/named/standard/loopback6.arpa
	ln -fs ${TZDIR}/${LOCALTIME} ${DESTDIR}/etc/localtime
	ln -fs /usr/sbin/rmt ${DESTDIR}/etc/rmt
	${INSTALL} -c -o root -g wheel -m 644 minfree \
	    ${DESTDIR}/var/crash
	${INSTALL} -c -o ${BINOWN} -g operator -m 664 /dev/null \
	    ${DESTDIR}/etc/dumpdates
	${INSTALL} -c -o root -g crontab -m 660 /dev/null \
	    ${DESTDIR}/var/cron/at.deny
	${INSTALL} -c -o root -g crontab -m 660 /dev/null \
	    ${DESTDIR}/var/cron/cron.deny
	${INSTALL} -c -o root -g wheel -m 600 /dev/null \
	    ${DESTDIR}/var/cron/log
	${INSTALL} -c -o root -g wheel -m 444 /dev/null \
	    ${DESTDIR}/var/db/locate.database
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/authlog
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/daemon
	${INSTALL} -c -o ${BINOWN} -g wheel -m 600 /dev/null \
	    ${DESTDIR}/var/log/failedlogin
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/ftpd
	${INSTALL} -c -o ${BINOWN} -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/log/lastlog
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/lpd-errs
	${INSTALL} -c -o ${BINOWN} -g wheel -m 600 /dev/null \
	    ${DESTDIR}/var/log/maillog
	${INSTALL} -c -o ${BINOWN} -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/log/messages
	${INSTALL} -c -o ${BINOWN} -g wheel -m 600 /dev/null \
	    ${DESTDIR}/var/log/secure
	${INSTALL} -c -o ${BINOWN} -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/log/wtmp
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/xferlog
	cd ../usr.sbin/ypserv/ypinit && exec ${MAKE} distribution
	cd ../usr.bin/ssh && exec ${MAKE} distribution
	cd ../lib/libcrypto && exec ${MAKE} distribution
	cd ../usr.bin/bgplg && exec ${MAKE} distribution
	cd ../usr.bin/mail && exec ${MAKE} distribution
	cd ../usr.sbin/sliplogin && exec ${MAKE} distribution
	cd ../usr.sbin/ldapd && exec ${MAKE} distribution
	cd ../usr.sbin/nginx && exec ${MAKE} -f Makefile.bsd-wrapper distribution
	cd ../usr.sbin/npppd && exec ${MAKE} distribution
	cd mail && exec ${MAKE} distribution
	${INSTALL} -c -o root -g wheel -m 440 ../usr.bin/sudo/sudoers \
	    ${DESTDIR}/etc/sudoers
	cd rc.d; \
		${INSTALL} -c -o root -g wheel -m 644 rc.subr \
		    ${DESTDIR}/etc/rc.d && \
		${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 555 ${RCDAEMONS} \
		    ${DESTDIR}/etc/rc.d

distribution:
	exec ${SUDO} ${MAKE} distribution-etc-root-var
	cd .. && exec ${SUDO} ${MAKE} install
	touch ${DESTDIR}/usr/share/sysmerge/etcsum
	touch ${DESTDIR}/usr/share/sysmerge/examplessum
	ETCLIST=`mktemp /tmp/_etclist.XXXXXXXXXX` || exit 1; \
	sort ../distrib/sets/lists/etc/{mi,md.${MACHINE}} > $${ETCLIST}; \
	cd ${DESTDIR} && \
		xargs sha256 -h ${DESTDIR}/usr/share/sysmerge/etcsum < $${ETCLIST} || true; \
	rm -f $${ETCLIST}
	EGLIST=`mktemp /tmp/_eglist.XXXXXXXXXX` || exit 1; \
	grep -h '/etc/examples/' ../distrib/sets/lists/base/{mi,md.${MACHINE}} | sort > $${EGLIST}; \
	cd ${DESTDIR} && \
		xargs sha256 -h ${DESTDIR}/usr/share/sysmerge/examplessum < $${EGLIST} || true; \
	rm -f $${EGLIST}

distrib-dirs:
	if [ ! -d ${DESTDIR}/. ]; then \
		${INSTALL} -d -o root -g wheel -m 755 ${DESTDIR}; \
	fi
	mtree -qdef mtree/4.4BSD.dist -p ${DESTDIR}/ -U
	if [ ! -d ${DESTDIR}/usr/src ]; then \
		${INSTALL} -d -o root -g wsrc -m 775 ${DESTDIR}/usr/src; \
	fi
	cd ${DESTDIR}/; ln -fhs usr/src/sys sys

.ifndef RELEASEDIR
release:
	@echo setenv RELEASEDIR before building a release.
	@false
.else

release-sets:
	cd ${RELEASEDIR} && rm -f SHA256
	cd ../distrib/sets && exec ${SUDO} sh maketars ${OSrev}

sha:
	-cd ${RELEASEDIR}; \
	    cksum -a sha256 \
	    ${ALL_KERNELS} ${MDEXT} ${MISETS} | sort > SHA256

release: sha
sha: distrib
distrib: release-sets kernels post-kernels
release-sets: distribution kernels

.endif

.endif	# DESTDIR check

distrib:
	cd ../distrib && \
	    ${MAKE} && exec ${SUDO} ${MAKE} install

.PHONY: distribution-etc-root-var distribution distrib-dirs \
	release allarchs kernels post-kernels release-sets m4 install-mtree \
	bootblocks ${ALL_KERNELS}

SUBDIR+= etc.amd64 etc.armv7

.include <bsd.subdir.mk>
.include <bsd.prog.mk>
