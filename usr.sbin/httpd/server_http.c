/*	$OpenBSD: server_http.c,v 1.42 2014/08/06 18:21:14 reyk Exp $	*/

/*
 * Copyright (c) 2006 - 2014 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/tree.h>
#include <sys/hash.h>

#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <pwd.h>
#include <syslog.h>
#include <event.h>
#include <fnmatch.h>

#include "httpd.h"
#include "http.h"

static int	 server_httpmethod_cmp(const void *, const void *);
static int	 server_httperror_cmp(const void *, const void *);
void		 server_httpdesc_free(struct http_descriptor *);

static struct httpd	*env = NULL;

static struct http_method	 http_methods[] = HTTP_METHODS;
static struct http_error	 http_errors[] = HTTP_ERRORS;

void
server_http(struct httpd *x_env)
{
	if (x_env != NULL)
		env = x_env;

	DPRINTF("%s: sorting lookup tables, pid %d", __func__, getpid());

	/* Sort the HTTP lookup arrays */
	qsort(http_methods, sizeof(http_methods) /
	    sizeof(http_methods[0]) - 1,
	    sizeof(http_methods[0]), server_httpmethod_cmp);
	qsort(http_errors, sizeof(http_errors) /
	    sizeof(http_errors[0]) - 1,
	    sizeof(http_errors[0]), server_httperror_cmp);
}

void
server_http_init(struct server *srv)
{
	/* nothing */
}

int
server_httpdesc_init(struct client *clt)
{
	struct http_descriptor	*desc;

	if ((desc = calloc(1, sizeof(*desc))) == NULL)
		return (-1);

	RB_INIT(&desc->http_headers);
	clt->clt_desc = desc;

	return (0);
}

void
server_httpdesc_free(struct http_descriptor *desc)
{
	if (desc->http_path != NULL) {
		free(desc->http_path);
		desc->http_path = NULL;
	}
	if (desc->http_query != NULL) {
		free(desc->http_query);
		desc->http_query = NULL;
	}
	if (desc->http_version != NULL) {
		free(desc->http_version);
		desc->http_version = NULL;
	}
	if (desc->http_host != NULL) {
		free(desc->http_host);
		desc->http_host = NULL;
	}
	kv_purge(&desc->http_headers);
	desc->http_lastheader = NULL;
}

void
server_read_http(struct bufferevent *bev, void *arg)
{
	struct client		*clt = arg;
	struct server_config	*srv_conf = clt->clt_srv_conf;
	struct http_descriptor	*desc = clt->clt_desc;
	struct evbuffer		*src = EVBUFFER_INPUT(bev);
	char			*line = NULL, *key, *value;
	const char		*errstr;
	size_t			 size, linelen;
	struct kv		*hdr = NULL;

	getmonotime(&clt->clt_tv_last);

	size = EVBUFFER_LENGTH(src);
	DPRINTF("%s: session %d: size %lu, to read %lld",
	    __func__, clt->clt_id, size, clt->clt_toread);
	if (!size) {
		clt->clt_toread = TOREAD_HTTP_HEADER;
		goto done;
	}

	while (!clt->clt_done && (line = evbuffer_readline(src)) != NULL) {
		linelen = strlen(line);

		/*
		 * An empty line indicates the end of the request.
		 * libevent already stripped the \r\n for us.
		 */
		if (!linelen) {
			clt->clt_done = 1;
			free(line);
			break;
		}
		key = line;

		/* Limit the total header length minus \r\n */
		clt->clt_headerlen += linelen;
		if (clt->clt_headerlen > SERVER_MAXHEADERLENGTH) {
			free(line);
			server_abort_http(clt, 413, "request too large");
			return;
		}

		/*
		 * The first line is the GET/POST/PUT/... request,
		 * subsequent lines are HTTP headers.
		 */
		if (++clt->clt_line == 1)
			value = strchr(key, ' ');
		else if (*key == ' ' || *key == '\t')
			/* Multiline headers wrap with a space or tab */
			value = NULL;
		else
			value = strchr(key, ':');
		if (value == NULL) {
			if (clt->clt_line == 1) {
				free(line);
				server_abort_http(clt, 400, "malformed");
				return;
			}

			/* Append line to the last header, if present */
			if (kv_extend(&desc->http_headers,
			    desc->http_lastheader, line) == NULL) {
				free(line);
				goto fail;
			}

			free(line);
			continue;
		}
		if (*value == ':') {
			*value++ = '\0';
			value += strspn(value, " \t\r\n");
		} else {
			*value++ = '\0';
		}

		DPRINTF("%s: session %d: header '%s: %s'", __func__,
		    clt->clt_id, key, value);

		/*
		 * Identify and handle specific HTTP request methods
		 */
		if (clt->clt_line == 1) {
			if ((desc->http_method = server_httpmethod_byname(key))
			    == HTTP_METHOD_NONE)
				goto fail;

			/*
			 * Decode request path and query
			 */
			desc->http_path = strdup(value);
			if (desc->http_path == NULL) {
				free(line);
				goto fail;
			}
			desc->http_version = strchr(desc->http_path, ' ');
			if (desc->http_version != NULL)
				*desc->http_version++ = '\0';
			desc->http_query = strchr(desc->http_path, '?');
			if (desc->http_query != NULL)
				*desc->http_query++ = '\0';

			/*
			 * Have to allocate the strings because they could
			 * be changed independetly by the filters later.
			 */
			if (desc->http_version != NULL &&
			    (desc->http_version =
			    strdup(desc->http_version)) == NULL) {
				free(line);
				goto fail;
			}
			if (desc->http_query != NULL &&
			    (desc->http_query =
			    strdup(desc->http_query)) == NULL) {
				free(line);
				goto fail;
			}
		} else if (desc->http_method != HTTP_METHOD_NONE &&
		    strcasecmp("Content-Length", key) == 0) {
			if (desc->http_method == HTTP_METHOD_TRACE ||
			    desc->http_method == HTTP_METHOD_CONNECT) {
				/*
				 * These method should not have a body
				 * and thus no Content-Length header.
				 */
				server_abort_http(clt, 400, "malformed");
				goto abort;
			}

			/*
			 * Need to read data from the client after the
			 * HTTP header.
			 * XXX What about non-standard clients not using
			 * the carriage return? And some browsers seem to
			 * include the line length in the content-length.
			 */
			clt->clt_toread = strtonum(value, 0, LLONG_MAX,
			    &errstr);
			if (errstr) {
				server_abort_http(clt, 500, errstr);
				goto abort;
			}
			if ((size_t)clt->clt_toread >
			    srv_conf->maxrequestbody) {
				server_abort_http(clt, 413, NULL);
				goto abort;
			}
		}

		if (strcasecmp("Transfer-Encoding", key) == 0 &&
		    strcasecmp("chunked", value) == 0)
			desc->http_chunked = 1;

		if (clt->clt_line != 1) {
			if ((hdr = kv_add(&desc->http_headers, key,
			    value)) == NULL) {
				free(line);
				goto fail;
			}
			desc->http_lastheader = hdr;
		}

		free(line);
	}
	if (clt->clt_done) {
		if (desc->http_method == HTTP_METHOD_NONE) {
			server_abort_http(clt, 406, "no method");
			return;
		}

		switch (desc->http_method) {
		case HTTP_METHOD_CONNECT:
			/* Data stream */
			clt->clt_toread = TOREAD_UNLIMITED;
			bev->readcb = server_read;
			break;
		case HTTP_METHOD_DELETE:
		case HTTP_METHOD_GET:
		case HTTP_METHOD_HEAD:
		case HTTP_METHOD_OPTIONS:
			clt->clt_toread = 0;
			break;
		case HTTP_METHOD_POST:
		case HTTP_METHOD_PUT:
		case HTTP_METHOD_RESPONSE:
			/* HTTP request payload */
			if (clt->clt_toread > 0)
				bev->readcb = server_read_httpcontent;

			/* Single-pass HTTP body */
			if (clt->clt_toread < 0) {
				clt->clt_toread = TOREAD_UNLIMITED;
				bev->readcb = server_read;
			}
			break;
		default:
			/* HTTP handler */
			clt->clt_toread = TOREAD_HTTP_HEADER;
			bev->readcb = server_read_http;
			break;
		}
		if (desc->http_chunked) {
			/* Chunked transfer encoding */
			clt->clt_toread = TOREAD_HTTP_CHUNK_LENGTH;
			bev->readcb = server_read_httpchunks;
		}

 done:
		if (clt->clt_toread != 0)
			bufferevent_disable(bev, EV_READ);
		server_response(env, clt);
		return;
	}
	if (clt->clt_done) {
		server_close(clt, "done");
		return;
	}
	if (EVBUFFER_LENGTH(src) && bev->readcb != server_read_http)
		bev->readcb(bev, arg);
	bufferevent_enable(bev, EV_READ);
	return;
 fail:
	server_abort_http(clt, 500, strerror(errno));
	return;
 abort:
	free(line);
}

void
server_read_httpcontent(struct bufferevent *bev, void *arg)
{
	struct client		*clt = arg;
	struct evbuffer		*src = EVBUFFER_INPUT(bev);
	size_t			 size;

	getmonotime(&clt->clt_tv_last);

	size = EVBUFFER_LENGTH(src);
	DPRINTF("%s: session %d: size %lu, to read %lld", __func__,
	    clt->clt_id, size, clt->clt_toread);
	if (!size)
		return;

	if (clt->clt_toread > 0) {
		/* Read content data */
		if ((off_t)size > clt->clt_toread) {
			size = clt->clt_toread;
			if (fcgi_add_stdin(clt, src) == -1)
				goto fail;
			clt->clt_toread = 0;
		} else {
			if (fcgi_add_stdin(clt, src) == -1)
				goto fail;
			clt->clt_toread -= size;
		}
		DPRINTF("%s: done, size %lu, to read %lld", __func__,
		    size, clt->clt_toread);
	}
	if (clt->clt_toread == 0) {
		fcgi_add_stdin(clt, NULL);
		clt->clt_toread = TOREAD_HTTP_HEADER;
		bufferevent_disable(bev, EV_READ);
		bev->readcb = server_read_http;
		return;
	}
	if (clt->clt_done)
		goto done;
	if (bev->readcb != server_read_httpcontent)
		bev->readcb(bev, arg);

	return;
 done:
	return;
 fail:
	server_close(clt, strerror(errno));
}

void
server_read_httpchunks(struct bufferevent *bev, void *arg)
{
	struct client		*clt = arg;
	struct evbuffer		*src = EVBUFFER_INPUT(bev);
	char			*line;
	long long		 llval;
	size_t			 size;

	getmonotime(&clt->clt_tv_last);

	size = EVBUFFER_LENGTH(src);
	DPRINTF("%s: session %d: size %lu, to read %lld", __func__,
	    clt->clt_id, size, clt->clt_toread);
	if (!size)
		return;

	if (clt->clt_toread > 0) {
		/* Read chunk data */
		if ((off_t)size > clt->clt_toread) {
			size = clt->clt_toread;
			if (server_bufferevent_write_chunk(clt, src, size)
			    == -1)
				goto fail;
			clt->clt_toread = 0;
		} else {
			if (server_bufferevent_write_buffer(clt, src) == -1)
				goto fail;
			clt->clt_toread -= size;
		}
		DPRINTF("%s: done, size %lu, to read %lld", __func__,
		    size, clt->clt_toread);
	}
	switch (clt->clt_toread) {
	case TOREAD_HTTP_CHUNK_LENGTH:
		line = evbuffer_readline(src);
		if (line == NULL) {
			/* Ignore empty line, continue */
			bufferevent_enable(bev, EV_READ);
			return;
		}
		if (strlen(line) == 0) {
			free(line);
			goto next;
		}

		/*
		 * Read prepended chunk size in hex, ignore the trailer.
		 * The returned signed value must not be negative.
		 */
		if (sscanf(line, "%llx", &llval) != 1 || llval < 0) {
			free(line);
			server_close(clt, "invalid chunk size");
			return;
		}

		if (server_bufferevent_print(clt, line) == -1 ||
		    server_bufferevent_print(clt, "\r\n") == -1) {
			free(line);
			goto fail;
		}
		free(line);

		if ((clt->clt_toread = llval) == 0) {
			DPRINTF("%s: last chunk", __func__);
			clt->clt_toread = TOREAD_HTTP_CHUNK_TRAILER;
		}
		break;
	case TOREAD_HTTP_CHUNK_TRAILER:
		/* Last chunk is 0 bytes followed by trailer and empty line */
		line = evbuffer_readline(src);
		if (line == NULL) {
			/* Ignore empty line, continue */
			bufferevent_enable(bev, EV_READ);
			return;
		}
		if (server_bufferevent_print(clt, line) == -1 ||
		    server_bufferevent_print(clt, "\r\n") == -1) {
			free(line);
			goto fail;
		}
		if (strlen(line) == 0) {
			/* Switch to HTTP header mode */
			clt->clt_toread = TOREAD_HTTP_HEADER;
			bev->readcb = server_read_http;
		}
		free(line);
		break;
	case 0:
		/* Chunk is terminated by an empty newline */
		line = evbuffer_readline(src);
		if (line != NULL)
			free(line);
		if (server_bufferevent_print(clt, "\r\n") == -1)
			goto fail;
		clt->clt_toread = TOREAD_HTTP_CHUNK_LENGTH;
		break;
	}

 next:
	if (clt->clt_done)
		goto done;
	if (EVBUFFER_LENGTH(src))
		bev->readcb(bev, arg);
	bufferevent_enable(bev, EV_READ);
	return;

 done:
	server_close(clt, "last http chunk read (done)");
	return;
 fail:
	server_close(clt, strerror(errno));
}

void
server_reset_http(struct client *clt)
{
	struct http_descriptor	*desc = clt->clt_desc;
	struct server		*srv = clt->clt_srv;

	server_httpdesc_free(desc);
	desc->http_method = 0;
	desc->http_chunked = 0;
	clt->clt_headerlen = 0;
	clt->clt_line = 0;
	clt->clt_done = 0;
	clt->clt_chunk = 0;
	clt->clt_bev->readcb = server_read_http;
	clt->clt_srv_conf = &srv->srv_conf;

	server_log(clt, NULL);
}

void
server_http_date(char *tmbuf, size_t len)
{
	time_t			 t;
	struct tm		 tm;

	/* New HTTP/1.1 RFC 7231 prefers IMF-fixdate from RFC 5322 */
	time(&t);
	gmtime_r(&t, &tm);
	strftime(tmbuf, len, "%a, %d %h %Y %T %Z", &tm);
}

const char *
server_http_host(struct sockaddr_storage *ss, char *buf, size_t len)
{
	char		hbuf[MAXHOSTNAMELEN];
	in_port_t	port;

	if (print_host(ss, buf, len) == NULL)
		return (NULL);

	port = ntohs(server_socket_getport(ss));
	if (port == HTTP_PORT)
		return (buf);

	switch (ss->ss_family) {
	case AF_INET:
		if ((size_t)snprintf(hbuf, sizeof(hbuf),
		    "%s:%u", buf, port) >= sizeof(hbuf))
			return (NULL);
		break;
	case AF_INET6:
		if ((size_t)snprintf(hbuf, sizeof(hbuf),
		    "[%s]:%u", buf, port) >= sizeof(hbuf))
			return (NULL);
		break;
	}

	if (strlcpy(buf, hbuf, len) >= len)
		return (NULL);

	return (buf);
}

void
server_abort_http(struct client *clt, u_int code, const char *msg)
{
	struct server		*srv = clt->clt_srv;
	struct server_config	*srv_conf = &srv->srv_conf;
	struct bufferevent	*bev = clt->clt_bev;
	const char		*httperr = NULL, *text = "";
	char			*httpmsg, *extraheader = NULL;
	char			 tmbuf[32], hbuf[128];
	const char		*style;

	if ((httperr = server_httperror_byid(code)) == NULL)
		httperr = "Unknown Error";

	if (bev == NULL)
		goto done;

	if (server_log_http(clt, code, 0) == -1)
		goto done;

	/* Some system information */
	if (print_host(&srv_conf->ss, hbuf, sizeof(hbuf)) == NULL)
		goto done;

	server_http_date(tmbuf, sizeof(tmbuf));

	/* Do not send details of the Internal Server Error */
	switch (code) {
	case 500:
		/* Do not send details of the Internal Server Error */
		break;
	case 301:
	case 302:
		if (asprintf(&extraheader, "Location: %s\r\n", msg) == -1) {
			code = 500;
			extraheader = NULL;
		}
		break;
	default:
		text = msg;
		break;
	}

	/* A CSS stylesheet allows minimal customization by the user */
	style = "body { background-color: white; color: black; font-family: "
	    "'Comic Sans MS', 'Chalkboard SE', 'Comic Neue', sans-serif; }";
	/* Generate simple HTTP+HTML error document */
	if (asprintf(&httpmsg,
	    "HTTP/1.0 %03d %s\r\n"
	    "Date: %s\r\n"
	    "Server: %s\r\n"
	    "Connection: close\r\n"
	    "Content-Type: text/html\r\n"
	    "%s"
	    "\r\n"
	    "<!DOCTYPE HTML PUBLIC "
	    "\"-//W3C//DTD HTML 4.01 Transitional//EN\">\n"
	    "<html>\n"
	    "<head>\n"
	    "<title>%03d %s</title>\n"
	    "<style type=\"text/css\"><!--\n%s\n--></style>\n"
	    "</head>\n"
	    "<body>\n"
	    "<h1>%s</h1>\n"
	    "<div id='m'>%s</div>\n"
	    "<hr><address>%s at %s port %d</address>\n"
	    "</body>\n"
	    "</html>\n",
	    code, httperr, tmbuf, HTTPD_SERVERNAME,
	    extraheader == NULL ? "" : extraheader,
	    code, httperr, style, httperr, text,
	    HTTPD_SERVERNAME, hbuf, ntohs(srv_conf->port)) == -1)
		goto done;

	/* Dump the message without checking for success */
	server_dump(clt, httpmsg, strlen(httpmsg));
	free(httpmsg);

 done:
	free(extraheader);
	if (asprintf(&httpmsg, "%s (%03d %s)", msg, code, httperr) == -1) {
		server_close(clt, msg);
	} else {
		server_close(clt, httpmsg);
		free(httpmsg);
	}
}

void
server_close_http(struct client *clt)
{
	struct http_descriptor *desc	= clt->clt_desc;

	if (desc == NULL)
		return;
	server_httpdesc_free(desc);
	free(desc);
}

int
server_response(struct httpd *httpd, struct client *clt)
{
	char			 path[MAXPATHLEN];
	char			 hostname[MAXHOSTNAMELEN];
	struct http_descriptor	*desc	= clt->clt_desc;
	struct server		*srv = clt->clt_srv;
	struct server_config	*srv_conf = &srv->srv_conf, *location;
	struct kv		*kv, key, *host;

	/* Canonicalize the request path */
	if (desc->http_path == NULL ||
	    canonicalize_path(desc->http_path, path, sizeof(path)) == NULL)
		goto fail;
	free(desc->http_path);
	if ((desc->http_path = strdup(path)) == NULL)
		goto fail;

	key.kv_key = "Host";
	if ((host = kv_find(&desc->http_headers, &key)) != NULL &&
	    host->kv_value == NULL)
		host = NULL;

	if (strcmp(desc->http_version, "HTTP/1.1") == 0) {
		/* Host header is mandatory */
		if (host == NULL)
			goto fail;

		/* Is the connection persistent? */
		key.kv_key = "Connection";
		if ((kv = kv_find(&desc->http_headers, &key)) != NULL &&
		    strcasecmp("close", kv->kv_value) == 0)
			clt->clt_persist = 0;
		else
			clt->clt_persist++;
	} else {
		/* Is the connection persistent? */
		key.kv_key = "Connection";
		if ((kv = kv_find(&desc->http_headers, &key)) != NULL &&
		    strcasecmp("keep-alive", kv->kv_value) == 0)
			clt->clt_persist++;
		else
			clt->clt_persist = 0;
	}

	if (clt->clt_persist >= srv_conf->maxrequests)
		clt->clt_persist = 0;

	/*
	 * Do we have a Host header and matching configuration?
	 * XXX the Host can also appear in the URL path.
	 */
	if (host != NULL) {
		/* XXX maybe better to turn srv_hosts into a tree */
		TAILQ_FOREACH(srv_conf, &srv->srv_hosts, entry) {
			if ((srv_conf->flags & SRVFLAG_LOCATION) == 0 &&
			    fnmatch(srv_conf->name, host->kv_value,
			    FNM_CASEFOLD) == 0) {
				/* Replace host configuration */
				clt->clt_srv_conf = srv_conf;
				srv_conf = NULL;
				break;
			}
		}
	}

	if (srv_conf != NULL) {
		/* Use the actual server IP address */
		if (server_http_host(&clt->clt_srv_ss, hostname,
		    sizeof(hostname)) == NULL)
			goto fail;
	} else {
		/* Host header was valid and found */
		if (strlcpy(hostname, host->kv_value, sizeof(hostname)) >=
		    sizeof(hostname))
			goto fail;
		srv_conf = clt->clt_srv_conf;
	}

	if ((desc->http_host = strdup(hostname)) == NULL)
		goto fail;

	/* Now search for the location */
	TAILQ_FOREACH(location, &srv->srv_hosts, entry) {
		if ((location->flags & SRVFLAG_LOCATION) &&
		    location->id == srv_conf->id &&
		    fnmatch(location->location, desc->http_path,
		    FNM_CASEFOLD) == 0) {
			/* Replace host configuration */
			clt->clt_srv_conf = srv_conf = location;
			break;
		}
	}

	if (srv_conf->flags & SRVFLAG_FCGI)
		return (server_fcgi(httpd, clt));
	return (server_file(httpd, clt));
 fail:
	server_abort_http(clt, 400, "bad request");
	return (-1);
}

int
server_response_http(struct client *clt, u_int code,
    struct media_type *media, size_t size)
{
	struct http_descriptor	*desc = clt->clt_desc;
	const char		*error;
	struct kv		*ct, *cl;
	char			 tmbuf[32];

	if (desc == NULL || (error = server_httperror_byid(code)) == NULL)
		return (-1);

	if (server_log_http(clt, code, size) == -1)
		return (-1);

	kv_purge(&desc->http_headers);

	/* Add error codes */
	if (kv_setkey(&desc->http_pathquery, "%lu", code) == -1 ||
	    kv_set(&desc->http_pathquery, "%s", error) == -1)
		return (-1);

	/* Add headers */
	if (kv_add(&desc->http_headers, "Server", HTTPD_SERVERNAME) == NULL)
		return (-1);

	/* Is it a persistent connection? */
	if (clt->clt_persist) {
		if (kv_add(&desc->http_headers,
		    "Connection", "keep-alive") == NULL)
			return (-1);
	} else if (kv_add(&desc->http_headers, "Connection", "close") == NULL)
		return (-1);

	/* Set media type */
	if ((ct = kv_add(&desc->http_headers, "Content-Type", NULL)) == NULL ||
	    kv_set(ct, "%s/%s",
	    media == NULL ? "application" : media->media_type,
	    media == NULL ? "octet-stream" : media->media_subtype) == -1)
		return (-1);

	/* Set content length, if specified */
	if (size && ((cl =
	    kv_add(&desc->http_headers, "Content-Length", NULL)) == NULL ||
	    kv_set(cl, "%ld", size) == -1))
		return (-1);

	/* Date header is mandatory and should be added last */
	server_http_date(tmbuf, sizeof(tmbuf));
	if (kv_add(&desc->http_headers, "Date", tmbuf) == NULL)
		return (-1);

	/* Write completed header */
	if (server_writeresponse_http(clt) == -1 ||
	    server_bufferevent_print(clt, "\r\n") == -1 ||
	    server_headers(clt, server_writeheader_http, NULL) == -1 ||
	    server_bufferevent_print(clt, "\r\n") == -1)
		return (-1);

	if (desc->http_method == HTTP_METHOD_HEAD) {
		bufferevent_enable(clt->clt_bev, EV_READ|EV_WRITE);
		if (clt->clt_persist)
			clt->clt_toread = TOREAD_HTTP_HEADER;
		else
			clt->clt_toread = TOREAD_HTTP_NONE;
		clt->clt_done = 0;
		return (0);
	}

	return (1);
}

int
server_writeresponse_http(struct client *clt)
{
	struct http_descriptor	*desc = (struct http_descriptor *)clt->clt_desc;

	DPRINTF("version: %s rescode: %s resmsg: %s", desc->http_version,
	    desc->http_rescode, desc->http_resmesg);

	if (server_bufferevent_print(clt, desc->http_version) == -1 ||
	    server_bufferevent_print(clt, " ") == -1 ||
	    server_bufferevent_print(clt, desc->http_rescode) == -1 ||
	    server_bufferevent_print(clt, " ") == -1 ||
	    server_bufferevent_print(clt, desc->http_resmesg) == -1)
		return (-1);

	return (0);
}

int
server_writeheader_http(struct client *clt, struct kv *hdr, void *arg)
{
	char			*ptr;
	const char		*key;

	if (hdr->kv_flags & KV_FLAG_INVALID)
		return (0);

	/* The key might have been updated in the parent */
	if (hdr->kv_parent != NULL && hdr->kv_parent->kv_key != NULL)
		key = hdr->kv_parent->kv_key;
	else
		key = hdr->kv_key;

	ptr = hdr->kv_value;
	if (server_bufferevent_print(clt, key) == -1 ||
	    (ptr != NULL &&
	    (server_bufferevent_print(clt, ": ") == -1 ||
	    server_bufferevent_print(clt, ptr) == -1 ||
	    server_bufferevent_print(clt, "\r\n") == -1)))
		return (-1);
	DPRINTF("%s: %s: %s", __func__, key,
	    hdr->kv_value == NULL ? "" : hdr->kv_value);

	return (0);
}

int
server_headers(struct client *clt,
    int (*hdr_cb)(struct client *, struct kv *, void *), void *arg)
{
	struct kv		*hdr, *kv;
	struct http_descriptor	*desc = (struct http_descriptor *)clt->clt_desc;

	RB_FOREACH(hdr, kvtree, &desc->http_headers) {
		if ((hdr_cb)(clt, hdr, arg) == -1)
			return (-1);
		TAILQ_FOREACH(kv, &hdr->kv_children, kv_entry) {
			if ((hdr_cb)(clt, kv, arg) == -1)
				return (-1);
		}
	}

	return (0);
}

enum httpmethod
server_httpmethod_byname(const char *name)
{
	enum httpmethod		 id = HTTP_METHOD_NONE;
	struct http_method	 method, *res = NULL;

	/* Set up key */
	method.method_name = name;

	if ((res = bsearch(&method, http_methods,
	    sizeof(http_methods) / sizeof(http_methods[0]) - 1,
	    sizeof(http_methods[0]), server_httpmethod_cmp)) != NULL)
		id = res->method_id;

	return (id);
}

const char *
server_httpmethod_byid(u_int id)
{
	const char	*name = NULL;
	int		 i;

	for (i = 0; http_methods[i].method_name != NULL; i++) {
		if (http_methods[i].method_id == id) {
			name = http_methods[i].method_name;
			break;
		}
	}

	return (name);
}

static int
server_httpmethod_cmp(const void *a, const void *b)
{
	const struct http_method *ma = a;
	const struct http_method *mb = b;

	/*
	 * RFC 2616 section 5.1.1 says that the method is case
	 * sensitive so we don't do a strcasecmp here.
	 */
	return (strcmp(ma->method_name, mb->method_name));
}

const char *
server_httperror_byid(u_int id)
{
	struct http_error	 error, *res = NULL;

	/* Set up key */
	error.error_code = (int)id;

	res = bsearch(&error, http_errors,
	    sizeof(http_errors) / sizeof(http_errors[0]) - 1,
	    sizeof(http_errors[0]), server_httperror_cmp);

	return (res->error_name);
}

static int
server_httperror_cmp(const void *a, const void *b)
{
	const struct http_error *ea = a;
	const struct http_error *eb = b;
	return (ea->error_code - eb->error_code);
}

int
server_log_http(struct client *clt, u_int code, size_t len)
{
	static char		 tstamp[64];
	static char		 ip[INET6_ADDRSTRLEN];
	time_t			 t;
	struct kv		 key, *agent, *referrer;
	struct tm		*tm;
	struct server_config	*srv_conf;
	struct http_descriptor	*desc;

	if ((srv_conf = clt->clt_srv_conf) == NULL)
		return (-1);
	if ((srv_conf->flags & SRVFLAG_LOG) == 0)
		return (0);
	if ((desc = clt->clt_desc) == NULL)
		return (-1);

	if ((t = time(NULL)) == -1)
		return (-1);
	if ((tm = localtime(&t)) == NULL)
		return (-1);
	if (strftime(tstamp, sizeof(tstamp), "%d/%b/%Y:%H:%M:%S %z", tm) == 0)
		return (-1);

	if (print_host(&clt->clt_ss, ip, sizeof(ip)) == NULL)
		return (-1);

	/*
	 * For details on common log format, see:
	 * https://httpd.apache.org/docs/current/mod/mod_log_config.html
	 *
	 * httpd's format is similar to these Apache LogFormats:
	 * "%v %h %l %u %t \"%r\" %>s %B"
	 * "%v %h %l %u %t \"%r\" %>s %B \"%{Referer}i\" \"%{User-agent}i\""
	 */
	switch (srv_conf->logformat) {
	case LOG_FORMAT_COMMON:
		if (evbuffer_add_printf(clt->clt_log,
		    "%s %s - - [%s] \"%s %s%s%s%s%s\" %03d %zu\n",
		    srv_conf->name, ip, tstamp,
		    server_httpmethod_byid(desc->http_method),
		    desc->http_path == NULL ? "" : desc->http_path,
		    desc->http_query == NULL ? "" : "?",
		    desc->http_query == NULL ? "" : desc->http_query,
		    desc->http_version == NULL ? "" : " ",
		    desc->http_version == NULL ? "" : desc->http_version,
		    code, len) == -1)
			return (-1);
		break;

	case LOG_FORMAT_COMBINED:
		key.kv_key = "Referer"; /* sic */
		if ((referrer = kv_find(&desc->http_headers, &key)) != NULL &&
		    referrer->kv_value == NULL)
			referrer = NULL;

		key.kv_key = "User-Agent";
		if ((agent = kv_find(&desc->http_headers, &key)) != NULL &&
		    agent->kv_value == NULL)
			agent = NULL;

		if (evbuffer_add_printf(clt->clt_log,
		    "%s %s - - [%s] \"%s %s%s%s%s%s\" %03d %zu \"%s\" \"%s\"\n",
		    srv_conf->name, ip, tstamp,
		    server_httpmethod_byid(desc->http_method),
		    desc->http_path == NULL ? "" : desc->http_path,
		    desc->http_query == NULL ? "" : "?",
		    desc->http_query == NULL ? "" : desc->http_query,
		    desc->http_version == NULL ? "" : " ",
		    desc->http_version == NULL ? "" : desc->http_version,
		    code, len,
		    referrer == NULL ? "" : referrer->kv_value,
		    agent == NULL ? "" : agent->kv_value) == -1)
			return (-1);
		break;

	case LOG_FORMAT_CONNECTION:
		if (evbuffer_add_printf(clt->clt_log, " [%s]",
		    desc->http_path == NULL ? "" : desc->http_path) == -1)
			return (-1);
		break;
	}

	return (0);
}
