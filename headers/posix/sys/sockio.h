/*
 * Copyright 2002-2012 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYS_SOCKIO_H
#define _SYS_SOCKIO_H


/*! Socket I/O control codes, usually via struct ifreq, most of them should
	be compatible with the BSDs.
*/


#ifndef SIOCADDRT
#define SIOCADDRT				8900	/* add route */
#endif
#ifndef SIOCDELRT
#define SIOCDELRT				8901	/* delete route */
#endif
#ifndef SIOCSIFADDR
#define SIOCSIFADDR				8902	/* set interface address */
#endif
#ifndef SIOCGIFADDR
#define SIOCGIFADDR				8903	/* get interface address */
#endif
#ifndef SIOCSIFDSTADDR
#define SIOCSIFDSTADDR			8904	/* set point-to-point address */
#endif
#ifndef SIOCGIFDSTADDR
#define SIOCGIFDSTADDR			8905	/* get point-to-point address */
#endif
#ifndef SIOCSIFFLAGS
#define SIOCSIFFLAGS			8906	/* set interface flags */
#endif
#ifndef SIOCGIFFLAGS
#define SIOCGIFFLAGS			8907	/* get interface flags */
#endif
#ifndef SIOCGIFBRDADDR
#define SIOCGIFBRDADDR			8908	/* get broadcast address */
#endif
#ifndef SIOCSIFBRDADDR
#define SIOCSIFBRDADDR			8909	/* set broadcast address */
#endif
#ifndef SIOCGIFCOUNT
#define SIOCGIFCOUNT			8910	/* count interfaces */
#endif
#ifndef SIOCGIFCONF
#define SIOCGIFCONF				8911	/* get interface list */
#endif
#ifndef SIOCGIFINDEX
#define SIOCGIFINDEX			8912	/* interface name -> index */
#endif
#ifndef SIOCGIFNAME
#define SIOCGIFNAME				8913	/* interface index -> name */
#endif
#ifndef SIOCGIFNETMASK
#define SIOCGIFNETMASK			8914	/* get net address mask */
#endif
#ifndef SIOCSIFNETMASK
#define SIOCSIFNETMASK			8915	/* set net address mask */
#endif
#ifndef SIOCGIFMETRIC
#define SIOCGIFMETRIC			8916	/* get interface metric */
#endif
#ifndef SIOCSIFMETRIC
#define SIOCSIFMETRIC			8917	/* set interface metric */
#endif
#ifndef SIOCDIFADDR
#define SIOCDIFADDR				8918	/* delete interface address */
#endif
#ifndef SIOCAIFADDR
#define SIOCAIFADDR				8919
	/* configure interface alias, ifaliasreq */
#endif
#ifndef SIOCADDMULTI
#define SIOCADDMULTI			8920	/* add multicast address */
#endif
#ifndef SIOCDELMULTI
#define SIOCDELMULTI			8921	/* delete multicast address */
#endif
#ifndef SIOCGIFMTU
#define SIOCGIFMTU				8922	/* get interface MTU */
#endif
#ifndef SIOCSIFMTU
#define SIOCSIFMTU				8923	/* set interface MTU */
#endif
#ifndef SIOCSIFMEDIA
#define SIOCSIFMEDIA			8924	/* set net media */
#endif
#ifndef SIOCGIFMEDIA
#define SIOCGIFMEDIA			8925	/* get net media */
#endif

#ifndef SIOCGRTSIZE
#define SIOCGRTSIZE				8926	/* get route table size */
#endif
#ifndef SIOCGRTTABLE
#define SIOCGRTTABLE			8927	/* get route table */
#endif
#ifndef SIOCGETRT
#define SIOCGETRT				8928
	/* get route information for destination */
#endif

#ifndef SIOCGIFSTATS
#define SIOCGIFSTATS			8929	/* get interface stats */
#endif
#ifndef SIOCGIFTYPE
#define SIOCGIFTYPE				8931	/* get interface type */
#endif

#ifndef SIOCSPACKETCAP
#define SIOCSPACKETCAP			8932
	/* Start capturing packets on an interface */
#endif
#ifndef SIOCCPACKETCAP
#define SIOCCPACKETCAP			8933
	/* Stop capturing packets on an interface */
#endif

#ifndef SIOCSHIWAT
#define SIOCSHIWAT				8934	/* set high watermark */
#endif
#ifndef SIOCGHIWAT
#define SIOCGHIWAT				8935	/* get high watermark */
#endif
#ifndef SIOCSLOWAT
#define SIOCSLOWAT				8936	/* set low watermark */
#endif
#ifndef SIOCGLOWAT
#define SIOCGLOWAT				8937	/* get low watermark */
#endif
#ifndef SIOCATMARK
#define SIOCATMARK				8938	/* at out-of-band mark? */
#endif
#ifndef SIOCSPGRP
#define SIOCSPGRP				8939	/* set process group */
#endif
#ifndef SIOCGPGRP
#define SIOCGPGRP				8940	/* get process group */
#endif

#ifndef SIOCGPRIVATE_0
#define SIOCGPRIVATE_0			8941	/* device private 0 */
#endif
#ifndef SIOCGPRIVATE_1
#define SIOCGPRIVATE_1			8942	/* device private 1 */
#endif
#ifndef SIOCSDRVSPEC
#define SIOCSDRVSPEC			8943	/* set driver-specific parameters */
#endif
#ifndef SIOCGDRVSPEC
#define SIOCGDRVSPEC			8944	/* get driver-specific parameters */
#endif

#ifndef SIOCSIFGENERIC
#define SIOCSIFGENERIC			8945	/* generic IF set op */
#endif
#ifndef SIOCGIFGENERIC
#define SIOCGIFGENERIC			8946	/* generic IF get op */
#endif

/* Haiku specific extensions */
#define B_SOCKET_REMOVE_ALIAS	8918	/* synonym for SIOCDIFADDR */
#define B_SOCKET_ADD_ALIAS		8919	/* synonym for SIOCAIFADDR */
#define B_SOCKET_SET_ALIAS		8947	/* set interface alias, ifaliasreq */
#define B_SOCKET_GET_ALIAS		8948	/* get interface alias, ifaliasreq */
#define B_SOCKET_COUNT_ALIASES	8949	/* count interface aliases */

#ifndef SIOCEND
#define SIOCEND					9000
#endif	/* SIOCEND >= highest SIOC* */


#endif	/* _SYS_SOCKIO_H */
