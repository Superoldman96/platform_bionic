/*	$NetBSD: gethnamaddr.c,v 1.91 2014/06/19 15:08:18 christos Exp $	*/

/*
 * ++Copyright++ 1985, 1988, 1993
 * -
 * Copyright (c) 1985, 1988, 1993
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */

#include <sys/cdefs.h>
#include <sys/types.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include "NetdClientDispatch.h"
#include "resolv_netid.h"
#include "resolv_private.h"
#include "resolv_cache.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>

#ifndef LOG_AUTH
# define LOG_AUTH 0
#endif

#define MULTI_PTRS_ARE_ALIASES 1	/* XXX - experimental */

#include "nsswitch.h"
#include <stdlib.h>
#include <string.h>

#include "hostent.h"

#include "private/bionic_defs.h"

#define maybe_ok(res, nm, ok) (((res)->options & RES_NOCHECKNAME) != 0U || \
                               (ok)(nm) != 0)
#define maybe_hnok(res, hn) maybe_ok((res), (hn), res_hnok)
#define maybe_dnok(res, dn) maybe_ok((res), (dn), res_dnok)

#define addalias(d, s, arr, siz) do {			\
	if (d >= &arr[siz]) {				\
		char **xptr = realloc(arr, (siz + 10) * sizeof(*arr)); \
		if (xptr == NULL)			\
			goto nospc;			\
		d = xptr + (d - arr);			\
		arr = xptr;				\
		siz += 10;				\
	}						\
	*d++ = s;					\
} while (/*CONSTCOND*/0)

#define setup(arr, siz) do {				\
	arr = malloc((siz = 10) * sizeof(*arr)); 	\
	if (arr == NULL)				\
		goto nospc;				\
} while (/*CONSTCOND*/0)

// This should be synchronized to ResponseCode.h
static const int DnsProxyQueryResult = 222;

static const char AskedForGot[] =
			  "gethostby*.getanswer: asked for \"%s\", got \"%s\"";

static const struct android_net_context NETCONTEXT_UNSET = {
	.app_mark = MARK_UNSET,
	.app_netid = NETID_UNSET,
	.dns_mark = MARK_UNSET,
	.dns_netid = NETID_UNSET,
	.uid = NET_CONTEXT_INVALID_UID
};

#define	MAXPACKET	(8*1024)

typedef union {
    HEADER hdr;
    u_char buf[MAXPACKET];
} querybuf;

typedef union {
    int32_t al;
    char ac;
} align;

#ifdef DEBUG
static void debugprintf(const char *, res_state, ...)
	__attribute__((__format__(__printf__, 1, 3)));
#endif
static struct hostent *getanswer(const querybuf *, int, const char *, int,
    res_state, struct hostent *, char *, size_t, int *);
static void map_v4v6_address(const char *, char *);
static void map_v4v6_hostent(struct hostent *, char **, char *);
static void addrsort(char **, int, res_state);

void ht_sethostent(int);
void ht_endhostent(void);
struct hostent *ht_gethostbyname(char *);
struct hostent *ht_gethostbyaddr(const char *, int, int);
void dns_service(void);
#undef dn_skipname
int dn_skipname(const u_char *, const u_char *);
static int _dns_gethtbyaddr(void *, void *, va_list);
static int _dns_gethtbyname(void *, void *, va_list);

static struct hostent *gethostbyname_internal(const char *, int, res_state,
    struct hostent *, char *, size_t, int *, const struct android_net_context *);
static struct hostent* android_gethostbyaddrfornetcontext_proxy_internal(const void*, socklen_t,
    int, struct hostent *, char *, size_t, int *, const struct android_net_context *);

static const ns_src default_dns_files[] = {
	{ NSSRC_FILES, 	NS_SUCCESS },
	{ NSSRC_DNS, 	NS_SUCCESS },
	{ 0, 0 }
};

static int h_errno_to_result(int* herrno_p) {
  // glibc considers ERANGE a special case (and BSD uses ENOSPC instead).
  if (*herrno_p == NETDB_INTERNAL && errno == ENOSPC) {
    errno = ERANGE;
    return errno;
  }
  // glibc considers HOST_NOT_FOUND not an error for the _r functions' return value.
  return (*herrno_p != HOST_NOT_FOUND) ? *herrno_p : 0;
}

#ifdef DEBUG
static void
debugprintf(const char *msg, res_state res, ...)
{
	_DIAGASSERT(msg != NULL);

	if (res->options & RES_DEBUG) {
		int save = errno;
		va_list ap;

		va_start (ap, res);
		vprintf(msg, ap);
		va_end (ap);

		errno = save;
	}
}
#else
# define debugprintf(msg, res, num) /*nada*/
#endif

#define BOUNDED_INCR(x) \
	do { \
		BOUNDS_CHECK(cp, x); \
		cp += (x); \
	} while (/*CONSTCOND*/0)

#define BOUNDS_CHECK(ptr, count) \
	do { \
		if (eom - (ptr) < (count)) \
			goto no_recovery; \
	} while (/*CONSTCOND*/0)

static struct hostent *
getanswer(const querybuf *answer, int anslen, const char *qname, int qtype,
    res_state res, struct hostent *hent, char *buf, size_t buflen, int *he)
{
	const HEADER *hp;
	const u_char *cp;
	int n;
	size_t qlen;
	const u_char *eom, *erdata;
	char *bp, **ap, **hap, *ep;
	int type, class, ancount, qdcount;
	int haveanswer, had_error;
	int toobig = 0;
	char tbuf[MAXDNAME];
	char **aliases;
	size_t maxaliases;
	char *addr_ptrs[MAXADDRS];
	const char *tname;
	int (*name_ok)(const char *);

	_DIAGASSERT(answer != NULL);
	_DIAGASSERT(qname != NULL);

	tname = qname;
	hent->h_name = NULL;
	eom = answer->buf + anslen;
	switch (qtype) {
	case T_A:
	case T_AAAA:
		name_ok = res_hnok;
		break;
	case T_PTR:
		name_ok = res_dnok;
		break;
	default:
	  *he = NO_RECOVERY;
		return NULL;	/* XXX should be abort(); */
	}

	setup(aliases, maxaliases);
	/*
	 * find first satisfactory answer
	 */
	hp = &answer->hdr;
	ancount = ntohs(hp->ancount);
	qdcount = ntohs(hp->qdcount);
	bp = buf;
	ep = buf + buflen;
	cp = answer->buf;
	BOUNDED_INCR(HFIXEDSZ);
	if (qdcount != 1)
		goto no_recovery;

	n = dn_expand(answer->buf, eom, cp, bp, (int)(ep - bp));
	if ((n < 0) || !maybe_ok(res, bp, name_ok))
		goto no_recovery;

	BOUNDED_INCR(n + QFIXEDSZ);
	if (qtype == T_A || qtype == T_AAAA) {
		/* res_send() has already verified that the query name is the
		 * same as the one we sent; this just gets the expanded name
		 * (i.e., with the succeeding search-domain tacked on).
		 */
		n = (int)strlen(bp) + 1;		/* for the \0 */
		if (n >= MAXHOSTNAMELEN)
			goto no_recovery;
		hent->h_name = bp;
		bp += n;
		/* The qname can be abbreviated, but h_name is now absolute. */
		qname = hent->h_name;
	}
	hent->h_aliases = ap = aliases;
	hent->h_addr_list = hap = addr_ptrs;
	*ap = NULL;
	*hap = NULL;
	haveanswer = 0;
	had_error = 0;
	while (ancount-- > 0 && cp < eom && !had_error) {
		n = dn_expand(answer->buf, eom, cp, bp, (int)(ep - bp));
		if ((n < 0) || !maybe_ok(res, bp, name_ok)) {
			had_error++;
			continue;
		}
		cp += n;			/* name */
		BOUNDS_CHECK(cp, 3 * INT16SZ + INT32SZ);
		type = _getshort(cp);
 		cp += INT16SZ;			/* type */
		class = _getshort(cp);
 		cp += INT16SZ + INT32SZ;	/* class, TTL */
		n = _getshort(cp);
		cp += INT16SZ;			/* len */
		BOUNDS_CHECK(cp, n);
		erdata = cp + n;
		if (class != C_IN) {
			/* XXX - debug? syslog? */
			cp += n;
			continue;		/* XXX - had_error++ ? */
		}
		if ((qtype == T_A || qtype == T_AAAA) && type == T_CNAME) {
			n = dn_expand(answer->buf, eom, cp, tbuf,
			    (int)sizeof tbuf);
			if ((n < 0) || !maybe_ok(res, tbuf, name_ok)) {
				had_error++;
				continue;
			}
			cp += n;
			if (cp != erdata)
				goto no_recovery;
			/* Store alias. */
			addalias(ap, bp, aliases, maxaliases);
			n = (int)strlen(bp) + 1;	/* for the \0 */
			if (n >= MAXHOSTNAMELEN) {
				had_error++;
				continue;
			}
			bp += n;
			/* Get canonical name. */
			n = (int)strlen(tbuf) + 1;	/* for the \0 */
			if (n > ep - bp || n >= MAXHOSTNAMELEN) {
				had_error++;
				continue;
			}
			strlcpy(bp, tbuf, (size_t)(ep - bp));
			hent->h_name = bp;
			bp += n;
			continue;
		}
		if (qtype == T_PTR && type == T_CNAME) {
			n = dn_expand(answer->buf, eom, cp, tbuf,
			    (int)sizeof tbuf);
			if (n < 0 || !maybe_dnok(res, tbuf)) {
				had_error++;
				continue;
			}
			cp += n;
			if (cp != erdata)
				goto no_recovery;
			/* Get canonical name. */
			n = (int)strlen(tbuf) + 1;	/* for the \0 */
			if (n > ep - bp || n >= MAXHOSTNAMELEN) {
				had_error++;
				continue;
			}
			strlcpy(bp, tbuf, (size_t)(ep - bp));
			tname = bp;
			bp += n;
			continue;
		}
		if (type != qtype) {
			if (type != T_KEY && type != T_SIG)
				syslog(LOG_NOTICE|LOG_AUTH,
	       "gethostby*.getanswer: asked for \"%s %s %s\", got type \"%s\"",
				       qname, p_class(C_IN), p_type(qtype),
				       p_type(type));
			cp += n;
			continue;		/* XXX - had_error++ ? */
		}
		switch (type) {
		case T_PTR:
			if (strcasecmp(tname, bp) != 0) {
				syslog(LOG_NOTICE|LOG_AUTH,
				       AskedForGot, qname, bp);
				cp += n;
				continue;	/* XXX - had_error++ ? */
			}
			n = dn_expand(answer->buf, eom, cp, bp, (int)(ep - bp));
			if ((n < 0) || !maybe_hnok(res, bp)) {
				had_error++;
				break;
			}
#if MULTI_PTRS_ARE_ALIASES
			cp += n;
			if (cp != erdata)
				goto no_recovery;
			if (!haveanswer)
				hent->h_name = bp;
			else
				addalias(ap, bp, aliases, maxaliases);
			if (n != -1) {
				n = (int)strlen(bp) + 1;	/* for the \0 */
				if (n >= MAXHOSTNAMELEN) {
					had_error++;
					break;
				}
				bp += n;
			}
			break;
#else
			hent->h_name = bp;
			if (res->options & RES_USE_INET6) {
				n = strlen(bp) + 1;	/* for the \0 */
				if (n >= MAXHOSTNAMELEN) {
					had_error++;
					break;
				}
				bp += n;
				map_v4v6_hostent(hent, &bp, ep);
			}
			goto success;
#endif
		case T_A:
		case T_AAAA:
			if (strcasecmp(hent->h_name, bp) != 0) {
				syslog(LOG_NOTICE|LOG_AUTH,
				       AskedForGot, hent->h_name, bp);
				cp += n;
				continue;	/* XXX - had_error++ ? */
			}
			if (n != hent->h_length) {
				cp += n;
				continue;
			}
			if (type == T_AAAA) {
				struct in6_addr in6;
				memcpy(&in6, cp, NS_IN6ADDRSZ);
				if (IN6_IS_ADDR_V4MAPPED(&in6)) {
					cp += n;
					continue;
				}
			}
			if (!haveanswer) {
				int nn;

				hent->h_name = bp;
				nn = (int)strlen(bp) + 1;	/* for the \0 */
				bp += nn;
			}

			bp += sizeof(align) -
			    (size_t)((u_long)bp % sizeof(align));

			if (bp + n >= ep) {
				debugprintf("size (%d) too big\n", res, n);
				had_error++;
				continue;
			}
			if (hap >= &addr_ptrs[MAXADDRS - 1]) {
				if (!toobig++) {
					debugprintf("Too many addresses (%d)\n",
						res, MAXADDRS);
				}
				cp += n;
				continue;
			}
			(void)memcpy(*hap++ = bp, cp, (size_t)n);
			bp += n;
			cp += n;
			if (cp != erdata)
				goto no_recovery;
			break;
		default:
			abort();
		}
		if (!had_error)
			haveanswer++;
	}
	if (haveanswer) {
		*ap = NULL;
		*hap = NULL;
		/*
		 * Note: we sort even if host can take only one address
		 * in its return structures - should give it the "best"
		 * address in that case, not some random one
		 */
		if (res->nsort && haveanswer > 1 && qtype == T_A)
			addrsort(addr_ptrs, haveanswer, res);
		if (!hent->h_name) {
			n = (int)strlen(qname) + 1;	/* for the \0 */
			if (n > ep - bp || n >= MAXHOSTNAMELEN)
				goto no_recovery;
			strlcpy(bp, qname, (size_t)(ep - bp));
			hent->h_name = bp;
			bp += n;
		}
		if (res->options & RES_USE_INET6)
			map_v4v6_hostent(hent, &bp, ep);
	  goto success;
	}
no_recovery:
	free(aliases);
	*he = NO_RECOVERY;
	return NULL;
success:
	bp = __builtin_align_up(bp, sizeof(uintptr_t));
	n = (int)(ap - aliases);
	qlen = (n + 1) * sizeof(*hent->h_aliases);
	if ((size_t)(ep - bp) < qlen)
		goto nospc;
	hent->h_aliases = (void *)bp;
	memcpy(bp, aliases, qlen);
	free(aliases);
	aliases = NULL;

	bp += qlen;
	n = (int)(hap - addr_ptrs);
	qlen = (n + 1) * sizeof(*hent->h_addr_list);
	if ((size_t)(ep - bp) < qlen)
		goto nospc;
	hent->h_addr_list = (void *)bp;
	memcpy(bp, addr_ptrs, qlen);
	*he = NETDB_SUCCESS;
	return hent;
nospc:
	free(aliases);
	errno = ENOSPC;
	*he = NETDB_INTERNAL;
	return NULL;
}

/* The prototype of gethostbyname_r is from glibc, not that in netbsd. */
int
gethostbyname_r(const char *name, struct hostent *hp, char *buf, size_t buflen,
    struct hostent **result, int *errorp)
{
	res_state res = __res_get_state();
	if (res == NULL) {
		*result = NULL;
		*errorp = NETDB_INTERNAL;
		return -1;
	}

	_DIAGASSERT(name != NULL);

	if (res->options & RES_USE_INET6) {
		*result = gethostbyname_internal(name, AF_INET6, res, hp, buf, buflen, errorp,
		                                 &NETCONTEXT_UNSET);
		if (*result) {
			__res_put_state(res);
			return 0;
		}
	}
	*result = gethostbyname_internal(name, AF_INET, res, hp, buf, buflen, errorp,
	                                 &NETCONTEXT_UNSET);
	return h_errno_to_result(errorp);
}

/* The prototype of gethostbyname2_r is from glibc, not that in netbsd. */
int
gethostbyname2_r(const char *name, int af, struct hostent *hp, char *buf,
    size_t buflen, struct hostent **result, int *errorp)
{
	res_state res = __res_get_state();
	if (res == NULL) {
		*result = NULL;
		*errorp = NETDB_INTERNAL;
		return -1;
	}
	*result = gethostbyname_internal(name, af, res, hp, buf, buflen, errorp,
	                                 &NETCONTEXT_UNSET);
	return h_errno_to_result(errorp);
}

static struct hostent *
android_read_hostent(FILE* proxy, struct hostent* hp, char* hbuf, size_t hbuflen, int *he)
{
	uint32_t size;
	char buf[4];
	if (fread(buf, 1, sizeof(buf), proxy) != sizeof(buf)) return NULL;

	// This is reading serialized data from system/netd/server/DnsProxyListener.cpp
	// and changes here need to be matched there.
	int result_code = strtol(buf, NULL, 10);
	if (result_code != DnsProxyQueryResult) {
		fread(&size, 1, sizeof(size), proxy);
		*he = HOST_NOT_FOUND;
		return NULL;
	}

	if (fread(&size, 1, sizeof(size), proxy) != sizeof(size)) return NULL;
	size = ntohl(size);

	memset(hp, 0, sizeof(*hp));
	char *ptr = hbuf;
	char *hbuf_end = hbuf + hbuflen;

	if (ptr + size > hbuf_end) {
		goto nospc;
	}
	if (fread(ptr, 1, size, proxy) != size) return NULL;
	hp->h_name = ptr;
	ptr += size;

	char *aliases_ptrs[MAXALIASES];
	char **aliases = &aliases_ptrs[0];

	while (1) {
		if (fread(&size, 1, sizeof(size), proxy) != sizeof(size)) return NULL;
		size = ntohl(size);

		if (size == 0) {
			*aliases = NULL;
			break;
		}
		if (ptr + size > hbuf_end) {
		  goto nospc;
		}
		if (fread(ptr, 1, size, proxy) != size) return NULL;
		if (aliases < &aliases_ptrs[MAXALIASES - 1]) {
		  *aliases++ = ptr;
		}
		ptr += size;
	}

	// Fix alignment after variable-length data.
	ptr = __builtin_align_up(ptr, sizeof(uintptr_t));

	int aliases_len = ((int)(aliases - aliases_ptrs) + 1) * sizeof(*hp->h_aliases);
	if (ptr + aliases_len > hbuf_end) {
		goto nospc;
	}
	hp->h_aliases = (void*)ptr;
	memcpy(ptr, aliases_ptrs, aliases_len);
	ptr += aliases_len;

	if (fread(&size, 1, sizeof(size), proxy) != sizeof(size)) return NULL;
	hp->h_addrtype = ntohl(size);

	if (fread(&size, 1, sizeof(size), proxy) != sizeof(size)) return NULL;
	hp->h_length = ntohl(size);

	char *addr_ptrs[MAXADDRS];
	char **addr_p = &addr_ptrs[0];

	while (1) {
		if (fread(&size, 1, sizeof(size), proxy) != sizeof(size)) return NULL;
		size = ntohl(size);
		if (size == 0) {
			*addr_p = NULL;
			break;
		}
		if (ptr + size > hbuf_end) {
		  goto nospc;
		}
		if (fread(ptr, 1, size, proxy) != size) return NULL;
		if (addr_p < &addr_ptrs[MAXADDRS - 1]) {
		  *addr_p++ = ptr;
		}
		ptr += size;
	}

	// Fix alignment after variable-length data.
	ptr = __builtin_align_up(ptr, sizeof(uintptr_t));

	int addrs_len = ((int)(addr_p - addr_ptrs) + 1) * sizeof(*hp->h_addr_list);
	if (ptr + addrs_len > hbuf_end) {
		goto nospc;
	}
	hp->h_addr_list = (void*)ptr;
	memcpy(ptr, addr_ptrs, addrs_len);
	*he = NETDB_SUCCESS;
	return hp;

nospc:
	*he = NETDB_INTERNAL;
	errno = ENOSPC;
	return NULL;
}

static struct hostent *
gethostbyname_internal_real(const char *name, int af, res_state res, struct hostent *hp, char *buf,
                            size_t buflen, int *he)
{
	const char *cp;
	struct getnamaddr info;
	char hbuf[MAXHOSTNAMELEN];
	size_t size;
	static const ns_dtab dtab[] = {
		NS_FILES_CB(_hf_gethtbyname, NULL)
		{ NSSRC_DNS, _dns_gethtbyname, NULL },	/* force -DHESIOD */
		NS_NIS_CB(_yp_gethtbyname, NULL)
		NS_NULL_CB
	};

	_DIAGASSERT(name != NULL);

	switch (af) {
	case AF_INET:
		size = NS_INADDRSZ;
		break;
	case AF_INET6:
		size = NS_IN6ADDRSZ;
		break;
	default:
		*he = NETDB_INTERNAL;
		errno = EAFNOSUPPORT;
		return NULL;
	}
	if (buflen < size)
		goto nospc;

	hp->h_addrtype = af;
	hp->h_length = (int)size;

	/*
	 * if there aren't any dots, it could be a user-level alias.
	 * this is also done in res_nquery() since we are not the only
	 * function that looks up host names.
	 */
	if (!strchr(name, '.') && (cp = res_hostalias(res, name,
	    hbuf, sizeof(hbuf))))
		name = cp;

	/*
	 * disallow names consisting only of digits/dots, unless
	 * they end in a dot.
	 */
	if (isdigit((u_char) name[0]))
		for (cp = name;; ++cp) {
			if (!*cp) {
				if (*--cp == '.')
					break;
				/*
				 * All-numeric, no dot at the end.
				 * Fake up a hostent as if we'd actually
				 * done a lookup.
				 */
				goto fake;
			}
			if (!isdigit((u_char) *cp) && *cp != '.')
				break;
		}
	if ((isxdigit((u_char) name[0]) && strchr(name, ':') != NULL) ||
	    name[0] == ':')
		for (cp = name;; ++cp) {
			if (!*cp) {
				if (*--cp == '.')
					break;
				/*
				 * All-IPv6-legal, no dot at the end.
				 * Fake up a hostent as if we'd actually
				 * done a lookup.
				 */
				goto fake;
			}
			if (!isxdigit((u_char) *cp) && *cp != ':' && *cp != '.')
				break;
		}

	*he = NETDB_INTERNAL;
	info.hp = hp;
	info.buf = buf;
	info.buflen = buflen;
	info.he = he;
	if (nsdispatch(&info, dtab, NSDB_HOSTS, "gethostbyname",
	    default_dns_files, name, strlen(name), af) != NS_SUCCESS)
		return NULL;
	*he = NETDB_SUCCESS;
	return hp;
nospc:
	*he = NETDB_INTERNAL;
	errno = ENOSPC;
	return NULL;
fake:
	HENT_ARRAY(hp->h_addr_list, 1, buf, buflen);
	HENT_ARRAY(hp->h_aliases, 0, buf, buflen);

	hp->h_aliases[0] = NULL;
	if (size > buflen)
		goto nospc;

	if (inet_pton(af, name, buf) <= 0) {
		*he = HOST_NOT_FOUND;
		return NULL;
	}
	hp->h_addr_list[0] = buf;
	hp->h_addr_list[1] = NULL;
	buf += size;
	buflen -= size;
	HENT_SCOPY(hp->h_name, name, buf, buflen);
	if (res->options & RES_USE_INET6)
		map_v4v6_hostent(hp, &buf, buf + buflen);
	*he = NETDB_SUCCESS;
	return hp;
}

static struct hostent *
gethostbyname_internal(const char *name, int af, res_state res, struct hostent *hp, char *hbuf,
                       size_t hbuflen, int *errorp, const struct android_net_context *netcontext)
{
	FILE* proxy = fdopen(__netdClientDispatch.dnsOpenProxy(), "r+");
	if (proxy == NULL) {
		// Either we're not supposed to be using the proxy or the proxy is unavailable.
		res_setnetcontext(res, netcontext);
		return gethostbyname_internal_real(name, af, res, hp, hbuf, hbuflen, errorp);
	}
	unsigned netid = __netdClientDispatch.netIdForResolv(netcontext->app_netid);

	// This is writing to system/netd/server/DnsProxyListener.cpp and changes
	// here need to be matched there.
	if (fprintf(proxy, "gethostbyname %u %s %d",
			netid,
			name == NULL ? "^" : name,
			af) < 0) {
		fclose(proxy);
		return NULL;
	}

	if (fputc(0, proxy) == EOF || fflush(proxy) != 0) {
		fclose(proxy);
		return NULL;
	}

	struct hostent* result = android_read_hostent(proxy, hp, hbuf, hbuflen, errorp);
	fclose(proxy);
	return result;
}

/* The prototype of gethostbyaddr_r is from glibc, not that in netbsd. */
int gethostbyaddr_r(const void *addr, socklen_t len, int af, struct hostent *hp, char *buf,
                    size_t buflen, struct hostent **result, int *h_errnop)
{
	*result = android_gethostbyaddrfornetcontext_proxy_internal(
		addr, len, af, hp, buf, buflen, h_errnop, &NETCONTEXT_UNSET);
	return h_errno_to_result(h_errnop);
}

static struct hostent *
android_gethostbyaddrfornetcontext_real(const void *addr, socklen_t len, int af, struct hostent *hp,
                                 char *buf, size_t buflen, int *he,
				 const struct android_net_context *netcontext)
{
	const u_char *uaddr = (const u_char *)addr;
	socklen_t size;
	struct getnamaddr info;
	static const ns_dtab dtab[] = {
		NS_FILES_CB(_hf_gethtbyaddr, NULL)
		{ NSSRC_DNS, _dns_gethtbyaddr, NULL },	/* force -DHESIOD */
		NS_NIS_CB(_yp_gethtbyaddr, NULL)
		NS_NULL_CB
	};

	_DIAGASSERT(addr != NULL);

	if (af == AF_INET6 && len == NS_IN6ADDRSZ &&
	    (IN6_IS_ADDR_LINKLOCAL((const struct in6_addr *)addr) ||
	     IN6_IS_ADDR_SITELOCAL((const struct in6_addr *)addr))) {
		*he = HOST_NOT_FOUND;
		return NULL;
	}
	if (af == AF_INET6 && len == NS_IN6ADDRSZ &&
	    (IN6_IS_ADDR_V4MAPPED((const struct in6_addr *)addr) ||
	     IN6_IS_ADDR_V4COMPAT((const struct in6_addr *)addr))) {
		/* Unmap. */
		uaddr += NS_IN6ADDRSZ - NS_INADDRSZ;
		addr = uaddr;
		af = AF_INET;
		len = NS_INADDRSZ;
	}
	switch (af) {
	case AF_INET:
		size = NS_INADDRSZ;
		break;
	case AF_INET6:
		size = NS_IN6ADDRSZ;
		break;
	default:
		errno = EAFNOSUPPORT;
		*he = NETDB_INTERNAL;
		return NULL;
	}
	if (size != len) {
		errno = EINVAL;
		*he = NETDB_INTERNAL;
		return NULL;
	}
	info.hp = hp;
	info.buf = buf;
	info.buflen = buflen;
	info.he = he;
	*he = NETDB_INTERNAL;
	if (nsdispatch(&info, dtab, NSDB_HOSTS, "gethostbyaddr",
	    default_dns_files, uaddr, len, af, netcontext) != NS_SUCCESS)
		return NULL;
	*he = NETDB_SUCCESS;
	return hp;
}

static struct hostent*
android_gethostbyaddrfornetcontext_proxy_internal(const void* addr, socklen_t len, int af,
                             struct hostent *hp, char *hbuf, size_t hbuflen, int *he,
                             const struct android_net_context *netcontext)
{
	FILE* proxy = fdopen(__netdClientDispatch.dnsOpenProxy(), "r+");
	if (proxy == NULL) {
		// Either we're not supposed to be using the proxy or the proxy is unavailable.
		return android_gethostbyaddrfornetcontext_real(addr,len, af, hp, hbuf, hbuflen, he, netcontext);
	}
	char buf[INET6_ADDRSTRLEN];  //big enough for IPv4 and IPv6
	const char * addrStr = inet_ntop(af, addr, buf, sizeof(buf));
	if (addrStr == NULL) {
		fclose(proxy);
		return NULL;
	}

	unsigned netid = __netdClientDispatch.netIdForResolv(netcontext->app_netid);

	if (fprintf(proxy, "gethostbyaddr %s %d %d %u",
			addrStr, len, af, netid) < 0) {
		fclose(proxy);
		return NULL;
	}

	if (fputc(0, proxy) == EOF || fflush(proxy) != 0) {
		fclose(proxy);
		return NULL;
	}

	struct hostent *result = android_read_hostent(proxy, hp, hbuf, hbuflen, he);
	fclose(proxy);
	return result;
}

struct hostent*
netbsd_gethostent_r(FILE *hf, struct hostent *hent, char *buf, size_t buflen, int *he)
{
	char *p, *name;
	char *cp, **q;
	int af, len;
	size_t anum;
	char **aliases;
	size_t maxaliases;
	struct in6_addr host_addr;

	if (hf == NULL) {
		*he = NETDB_INTERNAL;
		errno = EINVAL;
		return NULL;
	}
	p = NULL;
	setup(aliases, maxaliases);

	/* Allocate a new space to read file lines like upstream does.
	 * To keep reentrancy we cannot use __res_get_static()->hostbuf here,
	 * as the buffer may be used to store content for a previous hostent
	 * returned by non-reentrant functions like gethostbyname().
	 */
	const size_t line_buf_size = sizeof(__res_get_static()->hostbuf);
	if ((p = malloc(line_buf_size)) == NULL) {
	  goto nospc;
	}
	for (;;) {
		if (!fgets(p, line_buf_size, hf)) {
			free(p);
			free(aliases);
			*he = HOST_NOT_FOUND;
			return NULL;
    		}
		if (*p == '#') {
			continue;
		}
		if (!(cp = strpbrk(p, "#\n"))) {
			continue;
		}
		*cp = '\0';
		if (!(cp = strpbrk(p, " \t")))
			continue;
		*cp++ = '\0';
		if (inet_pton(AF_INET6, p, &host_addr) > 0) {
			af = AF_INET6;
			len = NS_IN6ADDRSZ;
		} else {
			if (inet_pton(AF_INET, p, &host_addr) <= 0)
				continue;

			res_state res = __res_get_state();
			if (res == NULL)
				goto nospc;
			if (res->options & RES_USE_INET6) {
				map_v4v6_address(buf, buf);
				af = AF_INET6;
				len = NS_IN6ADDRSZ;
			} else {
				af = AF_INET;
				len = NS_INADDRSZ;
			}
			__res_put_state(res);
		}

		/* if this is not something we're looking for, skip it. */
		if (hent->h_addrtype != 0 && hent->h_addrtype != af)
			continue;
		if (hent->h_length != 0 && hent->h_length != len)
			continue;

		while (*cp == ' ' || *cp == '\t')
			cp++;
		if ((cp = strpbrk(name = cp, " \t")) != NULL)
			*cp++ = '\0';
		q = aliases;
		while (cp && *cp) {
			if (*cp == ' ' || *cp == '\t') {
				cp++;
				continue;
			}
			addalias(q, cp, aliases, maxaliases);
			if ((cp = strpbrk(cp, " \t")) != NULL)
				*cp++ = '\0';
		}
		break;
	}
	hent->h_length = len;
	hent->h_addrtype = af;
	HENT_ARRAY(hent->h_addr_list, 1, buf, buflen);
	anum = (size_t)(q - aliases);
	HENT_ARRAY(hent->h_aliases, anum, buf, buflen);
	HENT_COPY(hent->h_addr_list[0], &host_addr, hent->h_length, buf,
	    buflen);
	hent->h_addr_list[1] = NULL;

	HENT_SCOPY(hent->h_name, name, buf, buflen);
	for (size_t i = 0; i < anum; i++)
		HENT_SCOPY(hent->h_aliases[i], aliases[i], buf, buflen);
	hent->h_aliases[anum] = NULL;

	*he = NETDB_SUCCESS;
	free(p);
	free(aliases);
	return hent;
nospc:
	free(p);
	free(aliases);
	errno = ENOSPC;
	*he = NETDB_INTERNAL;
	return NULL;
}

static void
map_v4v6_address(const char *src, char *dst)
{
	u_char *p = (u_char *)dst;
	char tmp[NS_INADDRSZ];
	int i;

	_DIAGASSERT(src != NULL);
	_DIAGASSERT(dst != NULL);

	/* Stash a temporary copy so our caller can update in place. */
	(void)memcpy(tmp, src, NS_INADDRSZ);
	/* Mark this ipv6 addr as a mapped ipv4. */
	for (i = 0; i < 10; i++)
		*p++ = 0x00;
	*p++ = 0xff;
	*p++ = 0xff;
	/* Retrieve the saved copy and we're done. */
	(void)memcpy(p, tmp, NS_INADDRSZ);
}

static void
map_v4v6_hostent(struct hostent *hp, char **bpp, char *ep)
{
	char **ap;

	_DIAGASSERT(hp != NULL);
	_DIAGASSERT(bpp != NULL);
	_DIAGASSERT(ep != NULL);

	if (hp->h_addrtype != AF_INET || hp->h_length != NS_INADDRSZ)
		return;
	hp->h_addrtype = AF_INET6;
	hp->h_length = NS_IN6ADDRSZ;
	for (ap = hp->h_addr_list; *ap; ap++) {
		int i = (int)(sizeof(align) -
		    (size_t)((u_long)*bpp % sizeof(align)));

		if (ep - *bpp < (i + NS_IN6ADDRSZ)) {
			/* Out of memory.  Truncate address list here.  XXX */
			*ap = NULL;
			return;
		}
		*bpp += i;
		map_v4v6_address(*ap, *bpp);
		*ap = *bpp;
		*bpp += NS_IN6ADDRSZ;
	}
}

static void
addrsort(char **ap, int num, res_state res)
{
	int i, j;
	char **p;
	short aval[MAXADDRS];
	int needsort = 0;

	_DIAGASSERT(ap != NULL);

	p = ap;
	for (i = 0; i < num; i++, p++) {
	    for (j = 0 ; (unsigned)j < res->nsort; j++)
		if (res->sort_list[j].addr.s_addr ==
		    (((struct in_addr *)(void *)(*p))->s_addr &
		    res->sort_list[j].mask))
			break;
	    aval[i] = j;
	    if (needsort == 0 && i > 0 && j < aval[i-1])
		needsort = i;
	}
	if (!needsort)
	    return;

	while (needsort < num) {
	    for (j = needsort - 1; j >= 0; j--) {
		if (aval[j] > aval[j+1]) {
		    char *hp;

		    i = aval[j];
		    aval[j] = aval[j+1];
		    aval[j+1] = i;

		    hp = ap[j];
		    ap[j] = ap[j+1];
		    ap[j+1] = hp;
		} else
		    break;
	    }
	    needsort++;
	}
}

/*ARGSUSED*/
static int
_dns_gethtbyname(void *rv, void *cb_data, va_list ap)
{
	querybuf *buf;
	int n, type;
	struct hostent *hp;
	const char *name;
	res_state res;
	struct getnamaddr *info = rv;

	_DIAGASSERT(rv != NULL);

	name = va_arg(ap, char *);
	/* NOSTRICT skip string len */(void)va_arg(ap, int);
	info->hp->h_addrtype = va_arg(ap, int);

	switch (info->hp->h_addrtype) {
	case AF_INET:
		info->hp->h_length = NS_INADDRSZ;
		type = T_A;
		break;
	case AF_INET6:
		info->hp->h_length = NS_IN6ADDRSZ;
		type = T_AAAA;
		break;
	default:
		return NS_UNAVAIL;
	}
	buf = malloc(sizeof(*buf));
	if (buf == NULL) {
		*info->he = NETDB_INTERNAL;
		return NS_NOTFOUND;
	}
	res = __res_get_state();
	if (res == NULL) {
		free(buf);
		return NS_NOTFOUND;
	}
	n = res_nsearch(res, name, C_IN, type, buf->buf, (int)sizeof(buf->buf));
	if (n < 0) {
		free(buf);
		debugprintf("res_nsearch failed (%d)\n", res, n);
		__res_put_state(res);
		return NS_NOTFOUND;
	}
	hp = getanswer(buf, n, name, type, res, info->hp, info->buf,
	    info->buflen, info->he);
	free(buf);
	__res_put_state(res);
	if (hp == NULL)
		switch (*info->he) {
		case HOST_NOT_FOUND:
			return NS_NOTFOUND;
		case TRY_AGAIN:
			return NS_TRYAGAIN;
		default:
			return NS_UNAVAIL;
		}
	return NS_SUCCESS;
}

/*ARGSUSED*/
static int
_dns_gethtbyaddr(void *rv, void	*cb_data, va_list ap)
{
	char qbuf[MAXDNAME + 1], *qp, *ep;
	int n;
	querybuf *buf;
	struct hostent *hp;
	const unsigned char *uaddr;
	int advance;
	res_state res;
	char *bf;
	size_t blen;
	struct getnamaddr *info = rv;
	const struct android_net_context *netcontext;

	_DIAGASSERT(rv != NULL);

	uaddr = va_arg(ap, unsigned char *);
	info->hp->h_length = va_arg(ap, int);
	info->hp->h_addrtype = va_arg(ap, int);
	netcontext = va_arg(ap, const struct android_net_context *);

	switch (info->hp->h_addrtype) {
	case AF_INET:
		(void)snprintf(qbuf, sizeof(qbuf), "%u.%u.%u.%u.in-addr.arpa",
		    (uaddr[3] & 0xff), (uaddr[2] & 0xff),
		    (uaddr[1] & 0xff), (uaddr[0] & 0xff));
		break;

	case AF_INET6:
		qp = qbuf;
		ep = qbuf + sizeof(qbuf) - 1;
		for (n = NS_IN6ADDRSZ - 1; n >= 0; n--) {
			advance = snprintf(qp, (size_t)(ep - qp), "%x.%x.",
			    uaddr[n] & 0xf,
			    ((unsigned int)uaddr[n] >> 4) & 0xf);
			if (advance > 0 && qp + advance < ep)
				qp += advance;
			else {
				*info->he = NETDB_INTERNAL;
				return NS_NOTFOUND;
			}
		}
		if (strlcat(qbuf, "ip6.arpa", sizeof(qbuf)) >= sizeof(qbuf)) {
			*info->he = NETDB_INTERNAL;
			return NS_NOTFOUND;
		}
		break;
	default:
		return NS_UNAVAIL;
	}

	buf = malloc(sizeof(*buf));
	if (buf == NULL) {
		*info->he = NETDB_INTERNAL;
		return NS_NOTFOUND;
	}
	res = __res_get_state();
	if (res == NULL) {
		free(buf);
		return NS_NOTFOUND;
	}
	res_setnetcontext(res, netcontext);
	n = res_nquery(res, qbuf, C_IN, T_PTR, buf->buf, (int)sizeof(buf->buf));
	if (n < 0) {
		free(buf);
		debugprintf("res_nquery failed (%d)\n", res, n);
		__res_put_state(res);
		return NS_NOTFOUND;
	}
	hp = getanswer(buf, n, qbuf, T_PTR, res, info->hp, info->buf,
	    info->buflen, info->he);
	free(buf);
	if (hp == NULL) {
		__res_put_state(res);
		switch (*info->he) {
		case HOST_NOT_FOUND:
			return NS_NOTFOUND;
		case TRY_AGAIN:
			return NS_TRYAGAIN;
		default:
			return NS_UNAVAIL;
		}
	}

	bf = (void *)(hp->h_addr_list + 2);
	blen = (size_t)(bf - info->buf);
	if (blen + info->hp->h_length > info->buflen)
		goto nospc;
	hp->h_addr_list[0] = bf;
	hp->h_addr_list[1] = NULL;
	(void)memcpy(bf, uaddr, (size_t)info->hp->h_length);
	if (info->hp->h_addrtype == AF_INET && (res->options & RES_USE_INET6)) {
		if (blen + NS_IN6ADDRSZ > info->buflen)
			goto nospc;
		map_v4v6_address(bf, bf);
		hp->h_addrtype = AF_INET6;
		hp->h_length = NS_IN6ADDRSZ;
	}

	__res_put_state(res);
	*info->he = NETDB_SUCCESS;
	return NS_SUCCESS;
nospc:
	errno = ENOSPC;
	*info->he = NETDB_INTERNAL;
	return NS_UNAVAIL;
}

#ifdef YP
/*ARGSUSED*/
static struct hostent *
_yp_hostent(char *line, int af, struct getnamaddr *info)
{
	struct in6_addr host_addrs[MAXADDRS];
	char **aliases;
	size_t maxaliases;
	char *p = line;
	char *cp, **q, *ptr;
	size_t len, anum, i;
	int addrok;
	int more;
	size_t naddrs;
	struct hostent *hp = info->hp;

	_DIAGASSERT(line != NULL);

	hp->h_name = NULL;
	hp->h_addrtype = af;
	switch (af) {
	case AF_INET:
		hp->h_length = NS_INADDRSZ;
		break;
	case AF_INET6:
		hp->h_length = NS_IN6ADDRSZ;
		break;
	default:
		return NULL;
	}
	setup(aliases, maxaliases);
	naddrs = 0;
	q = aliases;

nextline:
	/* check for host_addrs overflow */
	if (naddrs >= __arraycount(host_addrs))
		goto done;

	more = 0;
	cp = strpbrk(p, " \t");
	if (cp == NULL)
		goto done;
	*cp++ = '\0';

	/* p has should have an address */
	addrok = inet_pton(af, p, &host_addrs[naddrs]);
	if (addrok != 1) {
		/* skip to the next line */
		while (cp && *cp) {
			if (*cp == '\n') {
				cp++;
				goto nextline;
			}
			cp++;
		}
		goto done;
	}
	naddrs++;

	while (*cp == ' ' || *cp == '\t')
		cp++;
	p = cp;
	cp = strpbrk(p, " \t\n");
	if (cp != NULL) {
		if (*cp == '\n')
			more = 1;
		*cp++ = '\0';
	}
	if (!hp->h_name)
		hp->h_name = p;
	else if (strcmp(hp->h_name, p) == 0)
		;
	else
		addalias(q, p, aliases, maxaliases);
	p = cp;
	if (more)
		goto nextline;

	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (*cp == '\n') {
			cp++;
			goto nextline;
		}
		addalias(q, cp, aliases, maxaliases);
		cp = strpbrk(cp, " \t");
		if (cp != NULL)
			*cp++ = '\0';
	}

done:
	if (hp->h_name == NULL) {
		free(aliases);
		return NULL;
	}

	ptr = info->buf;
	len = info->buflen;

	anum = (size_t)(q - aliases);
	HENT_ARRAY(hp->h_addr_list, naddrs, ptr, len);
	HENT_ARRAY(hp->h_aliases, anum, ptr, len);

	for (i = 0; i < naddrs; i++)
		HENT_COPY(hp->h_addr_list[i], &host_addrs[i], hp->h_length,
		    ptr, len);
	hp->h_addr_list[naddrs] = NULL;

	HENT_SCOPY(hp->h_name, hp->h_name, ptr, len);

	for (i = 0; i < anum; i++)
		HENT_SCOPY(hp->h_aliases[i], aliases[i], ptr, len);
	hp->h_aliases[anum] = NULL;
	free(aliases);

	return hp;
nospc:
	free(aliases);
	*info->he = NETDB_INTERNAL;
	errno = ENOSPC;
	return NULL;
}

/*ARGSUSED*/
int
_yp_gethtbyaddr(void *rv, void *cb_data, va_list ap)
{
	struct hostent *hp = NULL;
	char *ypcurrent;
	int ypcurrentlen, r;
	char name[INET6_ADDRSTRLEN];	/* XXX enough? */
	const unsigned char *uaddr;
	int af;
	const char *map;
	struct getnamaddr *info = rv;

	_DIAGASSERT(rv != NULL);

	uaddr = va_arg(ap, unsigned char *);
	/* NOSTRICT skip len */(void)va_arg(ap, int);
	af = va_arg(ap, int);

	if (!__ypdomain) {
		if (_yp_check(&__ypdomain) == 0)
			return NS_UNAVAIL;
	}
	/*
	 * XXX unfortunately, we cannot support IPv6 extended scoped address
	 * notation here.  gethostbyaddr() is not scope-aware.  too bad.
	 */
	if (inet_ntop(af, uaddr, name, (socklen_t)sizeof(name)) == NULL)
		return NS_UNAVAIL;
	switch (af) {
	case AF_INET:
		map = "hosts.byaddr";
		break;
	default:
		map = "ipnodes.byaddr";
		break;
	}
	ypcurrent = NULL;
	r = yp_match(__ypdomain, map, name,
		(int)strlen(name), &ypcurrent, &ypcurrentlen);
	if (r == 0)
		hp = _yp_hostent(ypcurrent, af, info);
	else
		hp = NULL;
	free(ypcurrent);
	if (hp == NULL) {
		*info->he = HOST_NOT_FOUND;
		return NS_NOTFOUND;
	}
	return NS_SUCCESS;
}

/*ARGSUSED*/
int
_yp_gethtbyname(void *rv, void *cb_data, va_list ap)
{
	struct hostent *hp;
	char *ypcurrent;
	int ypcurrentlen, r;
	const char *name;
	int af;
	const char *map;
	struct getnamaddr *info = rv;

	_DIAGASSERT(rv != NULL);

	name = va_arg(ap, char *);
	/* NOSTRICT skip string len */(void)va_arg(ap, int);
	af = va_arg(ap, int);

	if (!__ypdomain) {
		if (_yp_check(&__ypdomain) == 0)
			return NS_UNAVAIL;
	}
	switch (af) {
	case AF_INET:
		map = "hosts.byname";
		break;
	default:
		map = "ipnodes.byname";
		break;
	}
	ypcurrent = NULL;
	r = yp_match(__ypdomain, map, name,
		(int)strlen(name), &ypcurrent, &ypcurrentlen);
	if (r == 0)
		hp = _yp_hostent(ypcurrent, af, info);
	else
		hp = NULL;
	free(ypcurrent);
	if (hp == NULL) {
		*info->he = HOST_NOT_FOUND;
		return NS_NOTFOUND;
	}
	return NS_SUCCESS;
}
#endif

/*
 * Non-reentrant versions.
 */

__BIONIC_WEAK_FOR_NATIVE_BRIDGE
struct hostent *
gethostbyname(const char *name)
{
	struct hostent *result = NULL;
	struct res_static* rs = __res_get_static();

	gethostbyname_r(name, &rs->host, rs->hostbuf, sizeof(rs->hostbuf), &result, &h_errno);
	return result;
}

struct hostent *
gethostbyname2(const char *name, int af)
{
	struct hostent *result = NULL;
	struct res_static* rs = __res_get_static();

	gethostbyname2_r(name, af, &rs->host, rs->hostbuf, sizeof(rs->hostbuf), &result, &h_errno);
	return result;
}

// android_gethostby*fornet can be called in two different contexts.
//  - In the proxy client context (proxy != NULL), |netid| is |app_netid|.
//  - In the proxy listener context (proxy == NULL), |netid| is |dns_netid|.
// The netcontext is constructed before checking which context we are in.
// Therefore, we have to populate both fields, and rely on the downstream code to check whether
// |proxy == NULL|, and use that info to query the field that matches the caller's intent.
static struct android_net_context make_context(unsigned netid, unsigned mark) {
	struct android_net_context netcontext = NETCONTEXT_UNSET;
	netcontext.app_netid = netid;
	netcontext.app_mark = mark;
	netcontext.dns_netid = netid;
	netcontext.dns_mark = mark;
	return netcontext;
}

struct hostent *
android_gethostbynamefornet(const char *name, int af, unsigned netid, unsigned mark)
{
	const struct android_net_context netcontext = make_context(netid, mark);
	return android_gethostbynamefornetcontext(name, af, &netcontext);
}

struct hostent *
android_gethostbynamefornetcontext(const char *name, int af,
	const struct android_net_context *netcontext)
{
	struct hostent *hp;
	res_state res = __res_get_state();
	if (res == NULL)
		return NULL;
	struct res_static* rs = __res_get_static();
	hp = gethostbyname_internal(name, af, res, &rs->host, rs->hostbuf, sizeof(rs->hostbuf),
	                            &h_errno, netcontext);
	__res_put_state(res);
	return hp;
}

__BIONIC_WEAK_FOR_NATIVE_BRIDGE
struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int af)
{
	return android_gethostbyaddrfornetcontext_proxy(addr, len, af, &NETCONTEXT_UNSET);
}

struct hostent *
android_gethostbyaddrfornet(const void *addr, socklen_t len, int af, unsigned netid, unsigned mark)
{
	const struct android_net_context netcontext = make_context(netid, mark);
	return android_gethostbyaddrfornetcontext(addr, len, af, &netcontext);
}

struct hostent *
android_gethostbyaddrfornetcontext(const void *addr, socklen_t len, int af,
	const struct android_net_context *netcontext)
{
	return android_gethostbyaddrfornetcontext_proxy(addr, len, af, netcontext);
}

__LIBC_HIDDEN__ struct hostent*
android_gethostbyaddrfornetcontext_proxy(const void* addr, socklen_t len, int af,
                                  const struct android_net_context *netcontext)
{
	struct res_static* rs = __res_get_static();
	return android_gethostbyaddrfornetcontext_proxy_internal(addr, len, af, &rs->host, rs->hostbuf,
                                                    sizeof(rs->hostbuf), &h_errno, netcontext);
}

struct hostent *
gethostent(void)
{
  struct res_static* rs = __res_get_static();
	if (!rs->hostf) {
	  sethostent_r(&rs->hostf);
	  if (!rs->hostf) {
	    h_errno = NETDB_INTERNAL;
	    return NULL;
	  }
	}
	memset(&rs->host, 0, sizeof(rs->host));
	return netbsd_gethostent_r(rs->hostf, &rs->host, rs->hostbuf, sizeof(rs->hostbuf), &h_errno);
}
