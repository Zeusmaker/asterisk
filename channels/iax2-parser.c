/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Inter-Asterisk eXchange
 * 
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <asterisk/frame.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "iax2.h"
#include "iax2-parser.h"


static void internaloutput(const char *str)
{
	printf(str);
}

static void internalerror(const char *str)
{
	fprintf(stderr, "WARNING: %s", str);
}

static void (*outputf)(const char *str) = internaloutput;
static void (*errorf)(const char *str) = internalerror;

static void dump_addr(char *output, int maxlen, void *value, int len)
{
	struct sockaddr_in sin;
	if (len == sizeof(sin)) {
		memcpy(&sin, value, len);
		snprintf(output, maxlen, "IPV4 %s:%d", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	} else {
		snprintf(output, maxlen, "Invalid Address");
	}
}

static void dump_string(char *output, int maxlen, void *value, int len)
{
	maxlen--;
	if (maxlen > len)
		maxlen = len;
	strncpy(output,value, maxlen);
	output[maxlen] = '\0';
}

static void dump_int(char *output, int maxlen, void *value, int len)
{
	if (len == sizeof(unsigned int))
		snprintf(output, maxlen, "%d", ntohl(*((unsigned int *)value)));
	else
		snprintf(output, maxlen, "Invalid INT");
}

static void dump_short(char *output, int maxlen, void *value, int len)
{
	if (len == sizeof(unsigned short))
		snprintf(output, maxlen, "%d", ntohs(*((unsigned short *)value)));
	else
		snprintf(output, maxlen, "Invalid SHORT");
}

static void dump_byte(char *output, int maxlen, void *value, int len)
{
	if (len == sizeof(unsigned char))
		snprintf(output, maxlen, "%d", ntohs(*((unsigned char *)value)));
	else
		snprintf(output, maxlen, "Invalid BYTE");
}

static struct iax2_ie {
	int ie;
	char *name;
	void (*dump)(char *output, int maxlen, void *value, int len);
} ies[] = {
	{ IAX_IE_CALLED_NUMBER, "CALLED NUMBER", dump_string },
	{ IAX_IE_CALLING_NUMBER, "CALLING NUMBER", dump_string },
	{ IAX_IE_CALLING_NUMBER, "ANI", dump_string },
	{ IAX_IE_CALLING_NAME, "CALLING NAME", dump_string },
	{ IAX_IE_CALLED_CONTEXT, "CALLED CONTEXT", dump_string },
	{ IAX_IE_USERNAME, "USERNAME", dump_string },
	{ IAX_IE_PASSWORD, "PASSWORD", dump_string },
	{ IAX_IE_CAPABILITY, "CAPABILITY", dump_int },
	{ IAX_IE_FORMAT, "FORMAT", dump_int },
	{ IAX_IE_LANGUAGE, "LANGUAGE", dump_string },
	{ IAX_IE_VERSION, "VERSION", dump_short },
	{ IAX_IE_ADSICPE, "ADSICPE", dump_short },
	{ IAX_IE_DNID, "DNID", dump_string },
	{ IAX_IE_AUTHMETHODS, "AUTHMETHODS", dump_short },
	{ IAX_IE_CHALLENGE, "CHALLENGE", dump_string },
	{ IAX_IE_MD5_RESULT, "MD5 RESULT", dump_string },
	{ IAX_IE_RSA_RESULT, "RSA RESULT", dump_string },
	{ IAX_IE_APPARENT_ADDR, "APPARENT ADDRESS", dump_addr },
	{ IAX_IE_REFRESH, "REFRESH", dump_short },
	{ IAX_IE_DPSTATUS, "DIALPLAN STATUS", dump_short },
	{ IAX_IE_CALLNO, "CALL NUMBER", dump_short },
	{ IAX_IE_CAUSE, "CAUSE", dump_string },
	{ IAX_IE_IAX_UNKNOWN, "UNKNOWN IAX CMD", dump_byte },
	{ IAX_IE_MSGCOUNT, "MESSAGE COUNT", dump_short },
	{ IAX_IE_AUTOANSWER, "AUTO ANSWER REQ" },
};

const char *iax_ie2str(int ie)
{
	int x;
	for (x=0;x<sizeof(ies) / sizeof(ies[0]); x++) {
		if (ies[x].ie == ie)
			return ies[x].name;
	}
	return "Unknown IE";
}

static void dump_ies(unsigned char *iedata, int len)
{
	int ielen;
	int ie;
	int x;
	int found;
	char interp[80];
	char tmp[256];
	if (len < 2)
		return;
	while(len > 2) {
		ie = iedata[0];
		ielen = iedata[1];
		if (ielen + 2> len) {
			snprintf(tmp, sizeof(tmp), "Total IE length of %d bytes exceeds remaining frame length of %d bytes\n", ielen + 2, len);
			outputf(tmp);
			return;
		}
		found = 0;
		for (x=0;x<sizeof(ies) / sizeof(ies[0]); x++) {
			if (ies[x].ie == ie) {
				if (ies[x].dump) {
					ies[x].dump(interp, sizeof(interp), iedata + 2, ielen);
					snprintf(tmp, sizeof(tmp), "   %-15.15s : %s\n", ies[x].name, interp);
					outputf(tmp);
				} else {
					snprintf(tmp, sizeof(tmp), "   %-15.15s : Present\n", ies[x].name);
					outputf(tmp);
				}
				found++;
			}
		}
		if (!found) {
			snprintf(tmp, sizeof(tmp), "   Unknown IE %03d  : Present\n", ie);
			outputf(tmp);
		}
		iedata += (2 + ielen);
		len -= (2 + ielen);
	}
	outputf("\n");
}

void iax_showframe(struct ast_iax2_frame *f, struct ast_iax2_full_hdr *fhi, int rx, struct sockaddr_in *sin, int datalen)
{
	char *frames[] = {
		"(0?)",
		"DTMF   ",
		"VOICE  ",
		"VIDEO  ",
		"CONTROL",
		"NULL   ",
		"IAX    ",
		"TEXT   ",
		"IMAGE  " };
	char *iaxs[] = {
		"(0?)",
		"NEW    ",
		"PING   ",
		"PONG   ",
		"ACK    ",
		"HANGUP ",
		"REJECT ",
		"ACCEPT ",
		"AUTHREQ",
		"AUTHREP",
		"INVAL  ",
		"LAGRQ  ",
		"LAGRP  ",
		"REGREQ ",
		"REGAUTH",
		"REGACK ",
		"REGREJ ",
		"REGREL ",
		"VNAK   ",
		"DPREQ  ",
		"DPREP  ",
		"DIAL   ",
		"TXREQ  ",
		"TXCNT  ",
		"TXACC  ",
		"TXREADY",
		"TXREL  ",
		"TXREJ  ",
		"QUELCH ",
		"UNQULCH",
		"POKE",
		"PAGE",
		"MWI",
		"UNSUPPORTED",
	};
	char *cmds[] = {
		"(0?)",
		"HANGUP ",
		"RING   ",
		"RINGING",
		"ANSWER ",
		"BUSY   ",
		"TKOFFHK ",
		"OFFHOOK" };
	struct ast_iax2_full_hdr *fh;
	char retries[20];
	char class2[20];
	char subclass2[20];
	char *class;
	char *subclass;
	char tmp[256];
	if (f) {
		fh = f->data;
		snprintf(retries, sizeof(retries), "%03d", f->retries);
	} else {
		fh = fhi;
		if (ntohs(fh->dcallno) & IAX_FLAG_RETRANS)
			strcpy(retries, "Yes");
		else
			strcpy(retries, "No");
	}
	if (!(ntohs(fh->scallno) & IAX_FLAG_FULL)) {
		/* Don't mess with mini-frames */
		return;
	}
	if (fh->type > sizeof(frames)/sizeof(char *)) {
		snprintf(class2, sizeof(class2), "(%d?)", fh->type);
		class = class2;
	} else {
		class = frames[(int)fh->type];
	}
	if (fh->type == AST_FRAME_DTMF) {
		sprintf(subclass2, "%c", fh->csub);
		subclass = subclass2;
	} else if (fh->type == AST_FRAME_IAX) {
		if (fh->csub >= sizeof(iaxs)/sizeof(iaxs[0])) {
			snprintf(subclass2, sizeof(subclass2), "(%d?)", fh->csub);
			subclass = subclass2;
		} else {
			subclass = iaxs[(int)fh->csub];
		}
	} else if (fh->type == AST_FRAME_CONTROL) {
		if (fh->csub > sizeof(cmds)/sizeof(char *)) {
			snprintf(subclass2, sizeof(subclass2), "(%d?)", fh->csub);
			subclass = subclass2;
		} else {
			subclass = cmds[(int)fh->csub];
		}
	} else {
		snprintf(subclass2, sizeof(subclass2), "%d", fh->csub);
		subclass = subclass2;
	}
snprintf(tmp, sizeof(tmp), 
"%s-Frame Retry[%s] -- OSeqno: %3.3d ISeqno: %3.3d Type: %s Subclass: %s\n",
	(rx ? "Rx" : "Tx"),
	retries, fh->oseqno, fh->iseqno, class, subclass);
	outputf(tmp);
snprintf(tmp, sizeof(tmp), 
"   Timestamp: %05dms  SCall: %5.5d  DCall: %5.5d [%s:%d]\n",
	ntohl(fh->ts),
	ntohs(fh->scallno) & ~IAX_FLAG_FULL, ntohs(fh->dcallno) & ~IAX_FLAG_RETRANS,
		inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
	outputf(tmp);
	if (fh->type == AST_FRAME_IAX)
		dump_ies(fh->iedata, datalen);
}

int iax_ie_append_raw(struct iax_ie_data *ied, unsigned char ie, void *data, int datalen)
{
	char tmp[256];
	if (datalen > (sizeof(ied->buf) - ied->pos)) {
		snprintf(tmp, sizeof(tmp), "Out of space for ie '%s' (%d), need %d have %d\n", iax_ie2str(ie), ie, datalen, sizeof(ied->buf) - ied->pos);
		errorf(tmp);
		return -1;
	}
	ied->buf[ied->pos++] = ie;
	ied->buf[ied->pos++] = datalen;
	memcpy(ied->buf + ied->pos, data, datalen);
	ied->pos += datalen;
	return 0;
}

int iax_ie_append_addr(struct iax_ie_data *ied, unsigned char ie, struct sockaddr_in *sin)
{
	return iax_ie_append_raw(ied, ie, sin, sizeof(struct sockaddr_in));
}

int iax_ie_append_int(struct iax_ie_data *ied, unsigned char ie, unsigned int value) 
{
	unsigned int newval;
	newval = htonl(value);
	return iax_ie_append_raw(ied, ie, &newval, sizeof(newval));
}

int iax_ie_append_short(struct iax_ie_data *ied, unsigned char ie, unsigned short value) 
{
	unsigned short newval;
	newval = htons(value);
	return iax_ie_append_raw(ied, ie, &newval, sizeof(newval));
}

int iax_ie_append_str(struct iax_ie_data *ied, unsigned char ie, unsigned char *str)
{
	return iax_ie_append_raw(ied, ie, str, strlen(str));
}

int iax_ie_append_byte(struct iax_ie_data *ied, unsigned char ie, unsigned char dat)
{
	return iax_ie_append_raw(ied, ie, &dat, 1);
}

int iax_ie_append(struct iax_ie_data *ied, unsigned char ie) 
{
	return iax_ie_append_raw(ied, ie, NULL, 0);
}

void iax_set_output(void (*func)(const char *))
{
	outputf = func;
}

void iax_set_error(void (*func)(const char *))
{
	errorf = func;
}
