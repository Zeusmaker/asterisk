/*
 * ENUM Support for Asterisk
 *
 * Copyright (C) 2003 Digium
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Funding provided by nic.at
 *
 * Distributed under the terms of the GNU GPL
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <unistd.h>
#include <errno.h>

#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/enum.h>
#include <asterisk/dns.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>

#define TOPLEV "e164.arpa."

static struct enum_search {
	char toplev[80];
	struct enum_search *next;
} *toplevs;

static int enumver = 0;

static ast_mutex_t enumlock = AST_MUTEX_INITIALIZER;

struct naptr {
	unsigned short order;
	unsigned short pref;
} __attribute__ ((__packed__));

static int parse_ie(unsigned char *data, int maxdatalen, unsigned char *src, int srclen)
{
	int len, olen;
	len = olen = (int)src[0];
	src++;
	srclen--;
	if (len > srclen) {
		ast_log(LOG_WARNING, "Want %d, got %d\n", len, srclen);
		return -1;
	}
	if (len > maxdatalen)
		len = maxdatalen;
	memcpy(data, src, len);
	return olen + 1;
}

static int parse_naptr(unsigned char *dst, int dstsize, char *tech, int techsize, unsigned char *answer, int len, char *naptrinput)
{
	unsigned char *oanswer = answer;
	unsigned char flags[80] = "";
	unsigned char services[80] = "";
	unsigned char regexp[80] = "";
	unsigned char repl[80] = "";
	unsigned char temp[80] = "";
	unsigned char delim;
	unsigned char *delim2;
	unsigned char *pattern, *subst, *d;
	int res;
	int regexp_len, size, backref;
	int d_len = sizeof(temp) - 1;
	regex_t preg;
	regmatch_t pmatch[9];

	
	if (len < sizeof(struct naptr)) {
		printf("Length too short\n");
		return -1;
	}
	answer += sizeof(struct naptr);
	len -= sizeof(struct naptr);
	if ((res = parse_ie(flags, sizeof(flags) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get flags\n");
		return -1; 
	} else { answer += res; len -= res; }
	if ((res = parse_ie(services, sizeof(services) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get services\n");
		return -1; 
	} else { answer += res; len -= res; }
	if ((res = parse_ie(regexp, sizeof(regexp) - 1, answer, len)) < 0)
		return -1; else { answer += res; len -= res; }
	if ((res = dn_expand(oanswer,answer + len,answer, repl, sizeof(repl) - 1)) < 0) {
		ast_log(LOG_WARNING, "Failed to expand hostname\n");
		return -1;
	} 

	ast_log(LOG_DEBUG, "input='%s', flags='%s', services='%s', regexp='%s', repl='%s'\n",
		    naptrinput, flags, services, regexp, repl);

	if (tolower(flags[0]) != 'u') {
		ast_log(LOG_WARNING, "Flag must be 'U' or 'u'.\n");
		return -1;
	}

	if ((!strncasecmp(services, "e2u+sip", 7)) || 
	    (!strncasecmp(services, "sip+e2u", 7))) {
		strncpy(tech, "sip", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+h323", 7)) || 
	    (!strncasecmp(services, "h323+e2u", 7))) {
		strncpy(tech, "h323", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+iax", 7)) || 
	    (!strncasecmp(services, "iax+e2u", 7))) {
		strncpy(tech, "iax", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+iax2", 7)) || 
	    (!strncasecmp(services, "iax2+e2u", 7))) {
		strncpy(tech, "iax2", techsize -1); 
	} else if ((!strncasecmp(services, "e2u+tel", 7)) || 
	    (!strncasecmp(services, "tel+e2u", 7))) {
		strncpy(tech, "tel", techsize -1); 
	} else if (strncasecmp(services, "e2u+voice:", 10)) {
		ast_log(LOG_WARNING, "Services must be e2u+sip, sip+e2u, e2u+h323, h323+e2u, e2u+iax, iax+e2u, e2u+iax2, iax2+e2u, e2u+tel, tel+e2u or e2u+voice:\n");
		return -1;
	}

	/* DEDBUGGING STUB
	strcpy(regexp, "!^\\+43(.*)$!\\1@bla.fasel!");
	*/

	regexp_len = strlen(regexp);
	if (regexp_len < 7) {
		ast_log(LOG_WARNING, "Regex too short to be meaningful.\n");
		return -1;
	} 


	delim = regexp[0];
	delim2 = strchr(regexp + 1, delim);
	if ((delim2 == NULL) || (regexp[regexp_len-1] != delim)) {
		ast_log(LOG_WARNING, "Regex delimiter error (on \"%s\").\n",regexp);
		return -1;
	}

	pattern = regexp + 1;
	*delim2 = 0;
	subst   = delim2 + 1;
	regexp[regexp_len-1] = 0;

#if 0
	printf("Pattern: %s\n", pattern);
	printf("Subst: %s\n", subst);
#endif

/*
 * now do the regex wizardry.
 */

	if (regcomp(&preg, pattern, REG_EXTENDED | REG_NEWLINE)) {
		ast_log(LOG_WARNING, "Regex compilation error (regex = \"%s\").\n",regexp);
		return -1;
	}

	if (preg.re_nsub > 9) {
		ast_log(LOG_WARNING, "Regex compilation error: too many subs.\n");
		regfree(&preg);
		return -1;
	}

	if (regexec(&preg, naptrinput, 9, pmatch, 0)) {
		ast_log(LOG_WARNING, "Regex match failed.\n");
		regfree(&preg);
		return -1;
	}
	regfree(&preg);

	d = temp; d_len--; 
	while( *subst && (d_len > 0) ) {
		if ((subst[0] == '\\') && isdigit(subst[1]) && (pmatch[subst[1]-'0'].rm_so != -1)) {
			backref = subst[1]-'0';
			size = pmatch[backref].rm_eo - pmatch[backref].rm_so;
			if (size > d_len) {
				ast_log(LOG_WARNING, "Not enough space during regex substitution.\n");
				return -1;
				}
			memcpy(d, naptrinput + pmatch[backref].rm_so, size);
			d += size;
			d_len -= size;
			subst += 2;
		} else if (isprint(*subst)) {
			*d++ = *subst++;
			d_len--;
		} else {
			ast_log(LOG_WARNING, "Error during regex substitution.\n");
			return -1;
		}
	}
	*d = 0;
	strncpy(dst, temp, dstsize);
	d = strchr(services, ':');
	if (d) 
		strncpy(tech, d+1, techsize -1); 
	return 0;
}

struct enum_context {
	char *dst;
	int dstlen;
	char *tech;
	int techlen;
	char *naptrinput;
};

static int enum_callback(void *context, u_char *answer, int len, u_char *fullanswer)
{
	struct enum_context *c = (struct enum_context *)context;

	if (parse_naptr(c->dst, c->dstlen, c->tech, c->techlen, answer, len, c->naptrinput))
		ast_log(LOG_WARNING, "Failed to parse naptr :(\n");

	if (strlen(c->dst))
		return 1;

	return 0;
}

int ast_get_enum(struct ast_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen)
{
	struct enum_context context;
	char tmp[259 + 80];
	char naptrinput[80] = "+";
	int pos = strlen(number) - 1;
	int newpos = 0;
	int ret = -1;
	struct enum_search *s = NULL;
	int version = -1;

	strncat(naptrinput, number, sizeof(naptrinput) - 2);

	context.naptrinput = naptrinput;
	context.dst = dst;
	context.dstlen = dstlen;
	context.tech = tech;
	context.techlen = techlen;

	if (pos > 128)
		pos = 128;
	while(pos >= 0) {
		tmp[newpos++] = number[pos--];
		tmp[newpos++] = '.';
	}
	
	if (chan && ast_autoservice_start(chan) < 0)
		return -1;

	for(;;) {
		ast_mutex_lock(&enumlock);
		if (version != enumver) {
			/* Ooh, a reload... */
			s = toplevs;
			version = enumver;
		} else {
			s = s->next;
		}
		if (s) {
			strcpy(tmp + newpos, s->toplev);
		}
		ast_mutex_unlock(&enumlock);
		if (!s)
			break;
		ret = ast_search_dns(&context, tmp, C_IN, T_NAPTR, enum_callback);
		if (ret > 0)
			break;
	}
	if (ret < 0) {
		ast_log(LOG_DEBUG, "No such number found: %s (%s)\n", tmp, strerror(errno));
		ret = 0;
	}
	if (chan)
		ret |= ast_autoservice_stop(chan);
	return ret;
}

static struct enum_search *enum_newtoplev(char *s)
{
	struct enum_search *tmp;
	tmp = malloc(sizeof(struct enum_search));
	if (tmp) {
		memset(tmp, 0, sizeof(struct enum_search));
		strncpy(tmp->toplev, s, sizeof(tmp->toplev) - 1);
	}
	return tmp;
}

int ast_enum_init(void)
{
	struct ast_config *cfg;
	struct enum_search *s, *sl;
	struct ast_variable *v;

	/* Destroy existing list */
	ast_mutex_lock(&enumlock);
	s = toplevs;
	while(s) {
		sl = s;
		s = s->next;
		free(sl);
	}
	toplevs = NULL;
	cfg = ast_load("enum.conf");
	if (cfg) {
		sl = NULL;
		v = ast_variable_browse(cfg, "general");
		while(v) {
			if (!strcasecmp(v->name, "search")) {
				s = enum_newtoplev(v->value);
				if (s) {
					if (sl)
						sl->next = s;
					else
						toplevs = s;
					sl = s;
				}
			}
			v = v->next;
		}
		ast_destroy(cfg);
	} else {
		toplevs = enum_newtoplev(TOPLEV);
	}
	enumver++;
	ast_mutex_unlock(&enumlock);
	return 0;
}

int ast_enum_reload(void)
{
	return ast_enum_init();
}
