/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * Original code by Andy Heffernan (ahh@juniper.net)
 */

#ifndef lint
static const char rcsid[] _U_ =
    "@(#) $Header: /tcpdump/master/tcpdump/print-pgm.c,v 1.1 2005-05-20 21:02:31 hannes Exp $";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <tcpdump-stdinc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface.h"
#include "extract.h"
#include "addrtoname.h"

#include "ip.h"
#include "ip6.h"
#include "ipproto.h"

/*
 * PGM header (RFC 3208)
 */
struct pgm_header {
    u_int16_t	pgm_sport;
    u_int16_t	pgm_dport;
    u_int8_t	pgm_type;
    u_int8_t	pgm_options;
    u_int16_t	pgm_sum;
    u_int8_t	pgm_gsid[6];
    u_int16_t	pgm_length;
};

struct pgm_spm {
    u_int32_t	pgms_seq;
    u_int32_t	pgms_trailseq;
    u_int32_t	pgms_leadseq;
    u_int16_t	pgms_nla_afi;
    u_int16_t	pgms_reserved;
    u_int8_t	pgms_nla[0];
    /* ... options */
};

struct pgm_nak {
    u_int32_t	pgmn_seq;
    u_int16_t	pgmn_source_afi;
    u_int16_t	pgmn_reserved;
    u_int8_t	pgmn_source[0];
    /* ... u_int16_t	pgmn_group_afi */
    /* ... u_int16_t	pgmn_reserved2; */
    /* ... u_int8_t	pgmn_group[0]; */
    /* ... options */
};

struct pgm_poll {
    u_int32_t	pgmp_seq;
    u_int16_t	pgmp_round;
    u_int16_t	pgmp_reserved;
    /* ... options */
};

struct pgm_polr {
    u_int32_t	pgmp_seq;
    u_int16_t	pgmp_round;
    u_int16_t	pgmp_subtype;
    u_int16_t	pgmp_nla_afi;
    u_int16_t	pgmp_reserved;
    u_int8_t	pgmp_nla[0];
    /* ... options */
};

struct pgm_data {
    u_int32_t	pgmd_seq;
    u_int32_t	pgmd_trailseq;
    /* ... options */
};

typedef enum _pgm_type {
    PGM_SPM = 0,		/* source path message */
    PGM_POLL = 1,		/* POLL Request */
    PGM_POLR = 2,		/* POLL Response */
    PGM_ODATA = 4,		/* original data */
    PGM_RDATA = 5,		/* repair data */
    PGM_NAK = 8,		/* NAK */
    PGM_NULLNAK = 9,		/* Null NAK */
    PGM_NCF = 10,		/* NAK Confirmation */
    PGM_ACK = 11,		/* ACK for congestion control */
    PGM_SPMR = 12,		/* SPM request */
    PGM_MAX = 255
} pgm_type;

#define PGM_OPT_BIT_PRESENT	0x01
#define PGM_OPT_BIT_NETWORK	0x02
#define PGM_OPT_BIT_VAR_PKTLEN	0x40
#define PGM_OPT_BIT_PARITY	0x80

#define PGM_OPT_LENGTH		0x00
#define PGM_OPT_FRAGMENT        0x01
#define PGM_OPT_NAK_LIST        0x02
#define PGM_OPT_JOIN            0x03
#define PGM_OPT_NAK_BO_IVL	0x04
#define PGM_OPT_NAK_BO_RNG	0x05

#define PGM_OPT_REDIRECT        0x07
#define PGM_OPT_PARITY_PRM      0x08
#define PGM_OPT_PARITY_GRP      0x09
#define PGM_OPT_CURR_TGSIZE     0x0A
#define PGM_OPT_NBR_UNREACH	0x0B
#define PGM_OPT_PATH_NLA	0x0C

#define PGM_OPT_SYN             0x0D
#define PGM_OPT_FIN             0x0E
#define PGM_OPT_RST             0x0F
#define PGM_OPT_CR		0x10
#define PGM_OPT_CRQST		0x11
     
#define PGM_OPT_MASK		0x7f

#define PGM_OPT_END		0x80    /* end of options marker */

#define PGM_MIN_OPT_LEN		4

#ifndef AFI_IP
#define AFI_IP		1
#define AFI_IP6	        2
#endif

void
pgm_print(register const u_char *bp, register u_int length,
	  register const u_char *bp2)
{
	register const struct pgm_header *pgm;
	register const struct ip *ip;
	register char ch;
	u_int16_t sport, dport;
	int addr_size;
	const void *nla;
	int nla_af;
	char nla_buf[INET6_ADDRSTRLEN];
#ifdef INET6
	register const struct ip6_hdr *ip6;
#endif

	pgm = (struct pgm_header *)bp;
	ip = (struct ip *)bp2;
#ifdef INET6
	if (IP_V(ip) == 6)
		ip6 = (struct ip6_hdr *)bp2;
	else
		ip6 = NULL;
#endif /*INET6*/
	ch = '\0';
	if (!TTEST(pgm->pgm_dport)) {
#ifdef INET6
		if (ip6) {
			(void)printf("%s > %s: [|pgm]",
				ip6addr_string(&ip6->ip6_src),
				ip6addr_string(&ip6->ip6_dst));
			return;
		} else
#endif /* INET6 */
		{
			(void)printf("%s > %s: [|pgm]",
				ipaddr_string(&ip->ip_src),
				ipaddr_string(&ip->ip_dst));
			return;
		}
	}

	sport = EXTRACT_16BITS(&pgm->pgm_sport);
	dport = EXTRACT_16BITS(&pgm->pgm_dport);

#ifdef INET6
	if (ip6) {
		if (ip6->ip6_nxt == IPPROTO_PGM) {
			(void)printf("%s.%s > %s.%s: ",
				ip6addr_string(&ip6->ip6_src),
				tcpport_string(sport),
				ip6addr_string(&ip6->ip6_dst),
				tcpport_string(dport));
		} else {
			(void)printf("%s > %s: ",
				tcpport_string(sport), tcpport_string(dport));
		}
	} else
#endif /*INET6*/
	{
		if (ip->ip_p == IPPROTO_PGM) {
			(void)printf("%s.%s > %s.%s: ",
				ipaddr_string(&ip->ip_src),
				tcpport_string(sport),
				ipaddr_string(&ip->ip_dst),
				tcpport_string(dport));
		} else {
			(void)printf("%s > %s: ",
				tcpport_string(sport), tcpport_string(dport));
		}
	}

	TCHECK(*pgm);

        (void)printf("PGM, length %u", pgm->pgm_length);

        if (!vflag)
            return;

        if (length > pgm->pgm_length)
            length = pgm->pgm_length;

	(void)printf(" 0x%02x%02x%02x%02x%02x%02x ",
		     pgm->pgm_gsid[0],
                     pgm->pgm_gsid[1],
                     pgm->pgm_gsid[2],
		     pgm->pgm_gsid[3],
                     pgm->pgm_gsid[4],
                     pgm->pgm_gsid[5]);
	switch (pgm->pgm_type) {
	case PGM_SPM: {
	    struct pgm_spm *spm;

	    spm = (struct pgm_spm *)(pgm + 1);
	    TCHECK(*spm);

	    switch (EXTRACT_16BITS(&spm->pgms_nla_afi)) {
	    case AFI_IP:
		addr_size = sizeof(struct in_addr);
		nla_af = AF_INET;
		break;
	    case AFI_IP6:
		addr_size = sizeof(struct in6_addr);
		nla_af = AF_INET6;
		break;
	    default:
		goto trunc;
		break;
	    }
	    bp = (u_char *) (spm + 1);
	    TCHECK2(*bp, addr_size);
	    nla = bp;
	    bp += addr_size;

	    inet_ntop(nla_af, nla, nla_buf, sizeof(nla_buf));
	    (void)printf("SPM seq %u trail %u lead %u nla %s",
			 EXTRACT_32BITS(&spm->pgms_seq),
                         EXTRACT_32BITS(&spm->pgms_trailseq),
			 EXTRACT_32BITS(&spm->pgms_leadseq),
                         nla_buf);
	    break;
	}

	case PGM_POLL: {
	    struct pgm_poll *poll;

	    poll = (struct pgm_poll *)(pgm + 1);
	    TCHECK(*poll);
	    (void)printf("POLL seq %u round %u",
			 EXTRACT_32BITS(&poll->pgmp_seq),
                         EXTRACT_16BITS(&poll->pgmp_round));
	    break;
	}
	case PGM_POLR: {
	    struct pgm_polr *polr;
	    u_int32_t ivl, rnd, mask;

	    polr = (struct pgm_polr *)(pgm + 1);
	    TCHECK(*polr);

	    switch (EXTRACT_16BITS(&polr->pgmp_nla_afi)) {
	    case AFI_IP:
		addr_size = sizeof(struct in_addr);
		nla_af = AF_INET;
		break;
	    case AFI_IP6:
		addr_size = sizeof(struct in6_addr);
		nla_af = AF_INET6;
		break;
	    default:
		goto trunc;
		break;
	    }
	    bp = (u_char *) (polr + 1);
	    TCHECK2(*bp, addr_size);
	    nla = bp;
	    bp += addr_size;

	    inet_ntop(nla_af, nla, nla_buf, sizeof(nla_buf));

	    TCHECK2(*bp, sizeof(u_int32_t));
	    ivl = EXTRACT_32BITS(*(u_int32_t *)bp);
	    bp += sizeof(u_int32_t);

	    TCHECK2(*bp, sizeof(u_int32_t));
	    rnd = EXTRACT_32BITS(*(u_int32_t *)bp);
	    bp += sizeof(u_int32_t);

	    TCHECK2(*bp, sizeof(u_int32_t));
	    mask = EXTRACT_32BITS(*(u_int32_t *)bp);
	    bp += sizeof(u_int32_t);

	    (void)printf("POLR seq %u round %u nla %s ivl %u rnd 0x%08x "
			 "mask 0x%08x", EXTRACT_32BITS(polr->pgmp_seq),
			 EXTRACT_16BITS(&polr->pgmp_round), nla_buf, ivl, rnd, mask);
	    break;
	}
	case PGM_ODATA: {
	    struct pgm_data *odata;

	    odata = (struct pgm_data *)(pgm + 1);
	    TCHECK(*odata);
	    (void)printf("ODATA trail %u seq %u",
			 EXTRACT_32BITS(odata->pgmd_trailseq), EXTRACT_32BITS(odata->pgmd_seq));
	    break;
	}

	case PGM_RDATA: {
	    struct pgm_data *rdata;

	    rdata = (struct pgm_data *)(pgm + 1);
	    TCHECK(*rdata);
	    (void)printf("RDATA trail %u seq %u",
			 EXTRACT_32BITS(rdata->pgmd_trailseq), EXTRACT_32BITS(rdata->pgmd_seq));
	    break;
	}

	case PGM_NAK:
	case PGM_NULLNAK:
	case PGM_NCF: {
	    struct pgm_nak *nak;
	    const void *source, *group;
	    int source_af, group_af;
	    char source_buf[INET6_ADDRSTRLEN], group_buf[INET6_ADDRSTRLEN];

	    nak = (struct pgm_nak *)(pgm + 1);
	    TCHECK(*nak);

	    /*
	     * Skip past the source, saving info along the way
	     * and stopping if we don't have enough.
	     */
	    switch (EXTRACT_16BITS(&nak->pgmn_source_afi)) {
	    case AFI_IP:
		addr_size = sizeof(struct in_addr);
		source_af = AF_INET;
		break;
	    case AFI_IP6:
		addr_size = sizeof(struct in6_addr);
		source_af = AF_INET6;
		break;
	    default:
		goto trunc;
		break;
	    }
	    bp = (u_char *) (nak + 1);
	    TCHECK2(*bp, addr_size);
	    source = bp;
	    bp += addr_size;

	    /*
	     * Skip past the group, saving info along the way
	     * and stopping if we don't have enough.
	     */
	    switch (EXTRACT_16BITS(bp)) {
	    case AFI_IP:
		addr_size = sizeof(struct in_addr);
		group_af = AF_INET;
		break;
	    case AFI_IP6:
		addr_size = sizeof(struct in6_addr);
		group_af = AF_INET6;
		break;
	    default:
		goto trunc;
		break;
	    }
	    bp += (2 * sizeof(u_int16_t));
	    TCHECK2(*bp, addr_size);
	    group = bp;
	    bp += addr_size;

	    /*
	     * Options decoding can go here.
	     */
	    inet_ntop(source_af, source, source_buf, sizeof(source_buf));
	    inet_ntop(group_af, group, group_buf, sizeof(group_buf));
	    switch (pgm->pgm_type) {
		case PGM_NAK:
		    (void)printf("NAK ");
		    break;
		case PGM_NULLNAK:
		    (void)printf("NNAK ");
		    break;
		case PGM_NCF:
		    (void)printf("NCF ");
		    break;
		default:
                    break;
	    }
	    (void)printf("(%s -> %s), seq %u",
			 source_buf, group_buf, EXTRACT_32BITS(nak->pgmn_seq));
	    break;
	}

	case PGM_SPMR:
	    (void)printf("SPMR");
	    break;

	default:
	    (void)printf("UNKNOWN type %0x02x", pgm->pgm_type);
	    break;

	}
	if (pgm->pgm_options & PGM_OPT_BIT_PRESENT) {      

	    /*
	     * make sure there's enough for the first option header
	     */
	    if (!TTEST2(*bp, PGM_MIN_OPT_LEN)) {
		(void)printf("[|OPT]");
		return;
	    } 
	    while (TTEST2(*bp, PGM_MIN_OPT_LEN)) {
		u_int8_t opt_type, opt_len, flags1, flags2;
		u_int32_t seq, len, offset;

		opt_type = *bp++;
		opt_len = *bp++;

		switch (opt_type & PGM_OPT_MASK) {
		case PGM_OPT_LENGTH:
		    len = EXTRACT_16BITS(bp);
		    bp += sizeof(u_int16_t);
		    (void)printf(" OPT[%d] %d", opt_len, len);
		    break;

		case PGM_OPT_FRAGMENT:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    seq = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    offset = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    len = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    (void)printf(" FRAG seq %u off %u len %u", seq, offset, len);
		    break;

		case PGM_OPT_NAK_LIST:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    opt_len -= sizeof(u_int32_t);	/* option header */
		    (void)printf(" NAK LIST");
		    while (opt_len) {
			TCHECK2(*bp, sizeof(u_int32_t));
			(void)printf(" %u", EXTRACT_32BITS(*(u_int32_t *)bp));
			bp += sizeof(u_int32_t);
			opt_len -= sizeof(u_int32_t);
		    }
		    break;

		case PGM_OPT_JOIN:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    seq = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    (void)printf(" JOIN %u", seq);
		    break;

		case PGM_OPT_NAK_BO_IVL:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    offset = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    seq = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    (void)printf(" BACKOFF ivl %u ivlseq %u", offset, seq);
		    break;

		case PGM_OPT_NAK_BO_RNG:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    offset = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    seq = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    (void)printf(" BACKOFF max %u min %u", offset, seq);
		    break;

		case PGM_OPT_REDIRECT:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    switch (EXTRACT_16BITS(bp)) {
		    case AFI_IP:
			addr_size = sizeof(struct in_addr);
			nla_af = AF_INET;
			break;
		    case AFI_IP6:
			addr_size = sizeof(struct in6_addr);
			nla_af = AF_INET6;
			break;
		    default:
			goto trunc;
			break;
		    }
		    bp += (2 * sizeof(u_int16_t));
		    TCHECK2(*bp, addr_size);
		    nla = bp;
		    bp += addr_size;

		    inet_ntop(nla_af, nla, nla_buf, sizeof(nla_buf));
		    (void)printf(" REDIRECT %s",  (char *)nla);
		    break;

		case PGM_OPT_PARITY_PRM:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    len = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    (void)printf(" PARITY MAXTGS %u", len);
		    break;

		case PGM_OPT_PARITY_GRP:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    seq = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    (void)printf(" PARITY GROUP %u", seq);
		    break;

		case PGM_OPT_CURR_TGSIZE:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    len = EXTRACT_32BITS(*(u_int32_t *)bp);
		    bp += sizeof(u_int32_t);
		    (void)printf(" PARITY ATGS %u", len);
		    break;

		case PGM_OPT_NBR_UNREACH:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    (void)printf(" NBR_UNREACH");
		    break;

		case PGM_OPT_PATH_NLA:
		    (void)printf(" PATH_NLA [%d]", opt_len);
		    bp += opt_len;
		    break;

		case PGM_OPT_SYN:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    (void)printf(" SYN");
		    break;

		case PGM_OPT_FIN:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    (void)printf(" FIN");
		    break;

		case PGM_OPT_RST:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    (void)printf(" RST");
		    break;

		case PGM_OPT_CR:
		    (void)printf(" CR");
		    bp += opt_len;
		    break;

		case PGM_OPT_CRQST:
		    flags1 = *bp++;
		    flags2 = *bp++;
		    (void)printf(" CRQST");
		    break;

		default:
		    if (!TTEST2(*bp, opt_len)) {
			(void)printf(" [|OPT]");
			return;
		    } 
		    (void)printf(" OPT_%02X [%d] ", opt_type, opt_len);
		    bp += opt_len;
		    break;
		}

		if (opt_type & PGM_OPT_END)
		    break;
	     }
	}

	(void)printf(" [%u]", EXTRACT_16BITS(&pgm->pgm_length));

	return;

trunc:
	fputs("[|pgm]", stdout);
	if (ch != '\0')
		putchar('>');
}