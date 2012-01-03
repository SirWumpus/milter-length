/*
 * milter-length.c
 *
 * Copyright 2005, 2012 by Anthony Howe. All rights reserved.
 *
 * The following should be added to the sendmail.mc file:
 *
 *	INPUT_MAIL_FILTER(
 *		`milter-length',
 *		`S=unix:/var/lib/milter-length/socket, T=S:30s;R:3m'
 *	)dnl
 */

/***********************************************************************
 *** Leave this header alone. Its generate from the configure script.
 ***********************************************************************/

#include "config.h"

/***********************************************************************
 *** You can change the stuff below if the configure script doesn't work.
 ***********************************************************************/

#ifndef RUN_AS_USER
#define RUN_AS_USER			"milter"
#endif

#ifndef RUN_AS_GROUP
#define RUN_AS_GROUP			"milter"
#endif

#ifndef MILTER_CF
#define MILTER_CF			"/etc/mail/" MILTER_NAME ".cf"
#endif

#ifndef PID_FILE
#define PID_FILE			"/var/run/milter/" MILTER_NAME ".pid"
#endif

#ifndef SOCKET_FILE
#define SOCKET_FILE			"/var/run/milter/" MILTER_NAME ".socket"
#endif

#ifndef WORK_DIR
#define WORK_DIR			"/var/tmp"
#endif

#ifndef SENDMAIL_CF
#define SENDMAIL_CF			"/etc/mail/sendmail.cf"
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

/* Re-assert this macro just in case. May cause a compiler warning. */
#define _REENTRANT	1

#include <com/snert/lib/version.h>

#include <sys/types.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/berkeley_db.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/smf.h>
#include <com/snert/lib/mail/smdb.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/getopt.h>

#if LIBSNERT_MAJOR < 1 || LIBSNERT_MINOR < 75
# error "LibSnert 1.75.8 or better is required"
#endif

#ifndef HAVE_DB_H
# error "LibSnert must be built with Berkeley DB support"
#endif

#ifdef MILTER_BUILD_STRING
# define MILTER_STRING	MILTER_NAME"/"MILTER_VERSION"."MILTER_BUILD_STRING
#else
# define MILTER_STRING	MILTER_NAME"/"MILTER_VERSION
#endif

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define	TAG_FORMAT		"%05d %s: "
#define	TAG_ARGS		data->work.cid, data->work.qid

#define X_SCANNED_BY		"X-Scanned-By"

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

typedef struct {
	smfWork work;
	unsigned long tally;			/* per message */
	unsigned long maxBytes;			/* per message */
	unsigned long maxBytesRcpt;		/* per message */
	unsigned long maxBytesConnection;	/* per connection */
	char line[SMTP_TEXT_LINE_LENGTH+1];	/* general purpose */
	char client_name[SMTP_DOMAIN_LENGTH+1];	/* per connection */
	char client_addr[IPV6_TAG_LENGTH+IPV6_STRING_LENGTH];	/* per connection */
} *workspace;

static Option optIntro		= { "",	NULL, "\n# " MILTER_NAME "/" MILTER_VERSION "." MILTER_BUILD_STRING "\n#\n# " MILTER_COPYRIGHT "\n#\n" };

static Option *optTable[] = {
	&optIntro,
	NULL
};

/***********************************************************************
 *** Handlers
 ***********************************************************************/

static unsigned long
getMsgSizeLimit(workspace data, char *value)
{
	char *stop;
	unsigned long size;

	size = ULONG_MAX;

	if (value != NULL) {
		size = strtoul(value, &stop, 10);

		switch (*stop) {
		case 'G': case 'g': size *= 1024;
		case 'M': case 'm': size *= 1024;
		case 'K': case 'k': size *= 1024;
		}
	}

	smfLog(SMF_LOG_DEBUG, TAG_FORMAT "getMsgSizeLimit(%lx, %s) size=%lu", TAG_ARGS, (long) data, value, size);

	return size;
}

/*
 * Open and allocate per-connection resources.
 */
static sfsistat
filterOpen(SMFICTX *ctx, char *client_name, _SOCK_ADDR *raw_client_addr)
{
	workspace data;
	char *value = NULL;

	if (raw_client_addr == NULL) {
		smfLog(SMF_LOG_TRACE, "filterOpen() got NULL socket address, accepting connection");
		goto error0;
	}

	if (raw_client_addr->sa_family != AF_INET
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	&& raw_client_addr->sa_family != AF_INET6
#endif
	) {
		smfLog(SMF_LOG_TRACE, "filterOpen() unsupported socket address type, accepting connection");
		goto error0;
	}

	if ((data = calloc(1, sizeof *data)) == NULL)
		goto error0;

	data->work.ctx = ctx;
	data->work.qid = smfNoQueue;
	data->work.cid = smfOpenProlog(ctx, client_name, raw_client_addr, data->client_addr, sizeof (data->client_addr));

	smfLog(SMF_LOG_TRACE, TAG_FORMAT "filterOpen(%lx, '%s', [%s])", TAG_ARGS, (long) ctx, client_name, data->client_addr);

	data->line[0] = '\0';
	data->work.replyLine[0] = '\0';
	data->maxBytesConnection = ULONG_MAX;

	if (smfi_setpriv(ctx, (void *) data) == MI_FAILURE) {
		syslog(LOG_ERR, "failed to save workspace");
		goto error1;
	}

	(void) TextCopy(data->client_name, sizeof (data->client_name), client_name);

	/* Lookup
	 *
	 *	milter-length-connect:a.b.c.d			RHS
	 *	milter-length-connect:a.b.c			RHS
	 *	milter-length-connect:a.b			RHS
	 *	milter-length-connect:a				RHS
	 *
	 *	milter-length-connect:ipv6:a:b:c:d:e:f:g:h	RHS
	 *	milter-length-connect:ipv6:a:b:c:d:e:f:g	RHS
	 *	milter-length-connect:ipv6:a:b:c:d:e:f		RHS
	 *	milter-length-connect:ipv6:a:b:c:d:e		RHS
	 *	milter-length-connect:ipv6:a:b:c:d		RHS
	 *	milter-length-connect:ipv6:a:b:c		RHS
	 *	milter-length-connect:ipv6:a:b			RHS
	 *	milter-length-connect:ipv6:a			RHS
	 *
	 *	milter-length-connect:[ip]			RHS
	 *	milter-length-connect:[ipv6:ip]			RHS
	 *	milter-length-connect:some.sub.domain.tld	RHS
	 *	milter-length-connect:sub.domain.tld		RHS
	 *	milter-length-connect:domain.tld		RHS
	 *	milter-length-connect:tld			RHS
	 *	milter-length-connect:				RHS
	 */
	if (smfAccessClient(&data->work, MILTER_NAME "-connect:", data->client_name, data->client_addr, NULL, &value) != SMDB_ACCESS_NOT_FOUND) {
		data->maxBytesConnection = getMsgSizeLimit(data, value);
		free(value);
	}

	return SMFIS_CONTINUE;
error1:
	free(data);
error0:
	return SMFIS_ACCEPT;
}

static sfsistat
filterMail(SMFICTX *ctx, char **args)
{
	workspace data;
	ParsePath *path;
	const char *error;
	char *value = NULL, *auth_authen;

	if ((data = (workspace) smfi_getpriv(ctx)) == NULL)
		return smfNullWorkspaceError("filterMail");

	if ((data->work.qid = smfi_getsymval(ctx, "i")) == NULL)
		data->work.qid = smfNoQueue;

	data->tally = 0;
	data->maxBytesRcpt = 0;
	data->maxBytes = data->maxBytesConnection;
	auth_authen = smfi_getsymval(ctx, smMacro_auth_authen);

	smfLog(SMF_LOG_TRACE, TAG_FORMAT "filterMail(%lx, %lx) MAIL='%s' auth='%s'", TAG_ARGS, (long) ctx, (long) args, args[0], auth_authen);

	if (args[0] == NULL)
		return SMFIS_CONTINUE;

	if (smfOptSmtpAuthOk.value && auth_authen != NULL) {
		smfLog(SMF_LOG_INFO, TAG_FORMAT "authenticated \"%s\", accept", TAG_ARGS, auth_authen);
		return SMFIS_ACCEPT;
	}

	if ((error = parsePath(args[0], smfFlags, 1, &path)) != NULL)
		return smfReply(&data->work, 553, NULL, error);

	/* Skip the null address. */
	if (*path->address.string == '\0')
		;

	/* Lookup
	 *
	 *	milter-length-auth:auth_authen			RHS
	 *	milter-length-auth:				RHS
	 */
	else if (smfAccessAuth(&data->work, MILTER_NAME "-auth:", auth_authen, path->address.string, NULL, &value) != SMDB_ACCESS_NOT_FOUND) {
		data->maxBytes = getMsgSizeLimit(data, value);
	}

	/* Lookup
	 *
	 *	milter-length-from:account@some.sub.domain.tld	RHS
	 *	milter-length-from:some.sub.domain.tld		RHS
	 *	milter-length-from:sub.domain.tld		RHS
	 *	milter-length-from:domain.tld			RHS
	 *	milter-length-from:tld				RHS
	 *	milter-length-from:account@			RHS
	 *	milter-length-from:				RHS
	 */
	else if (smfAccessEmail(&data->work, MILTER_NAME "-from:", path->address.string, NULL, &value) != SMDB_ACCESS_NOT_FOUND) {
		data->maxBytes = getMsgSizeLimit(data, value);
	}

	free(value);
	free(path);

	return SMFIS_CONTINUE;
}

static sfsistat
filterRcpt(SMFICTX *ctx, char **args)
{
	workspace data;
	ParsePath *path;
	const char *error;
	char *value = NULL;
	unsigned long messageSizeLimit;

	if ((data = (workspace) smfi_getpriv(ctx)) == NULL)
		return smfNullWorkspaceError("filterRcpt");

	smfLog(SMF_LOG_TRACE, TAG_FORMAT "filterRcpt(%lx, %lx) RCPT='%s'", TAG_ARGS, (long) ctx, (long) args, args[0]);

	if (args[0] == NULL)
		return SMFIS_CONTINUE;

	if ((error = parsePath(args[0], smfFlags, 0, &path)) != NULL)
		return smfReply(&data->work, 553, NULL, error);

	/* Lookup
	 *
	 *	milter-length-to:account@some.sub.domain.tld	RHS
	 *	milter-length-to:some.sub.domain.tld		RHS
	 *	milter-length-to:sub.domain.tld			RHS
	 *	milter-length-to:domain.tld			RHS
	 *	milter-length-to:tld				RHS
	 *	milter-length-to:account@			RHS
	 *	milter-length-to:				RHS
	 */
	if (smfAccessEmail(&data->work, MILTER_NAME "-to:", path->address.string, NULL, &value) != SMDB_ACCESS_NOT_FOUND) {
		messageSizeLimit = getMsgSizeLimit(data, value);
		if (data->maxBytesRcpt < messageSizeLimit)
			data->maxBytesRcpt = messageSizeLimit;
	}

	free(value);
	free(path);

	return SMFIS_CONTINUE;
}

static sfsistat
filterBody(SMFICTX *ctx, unsigned char *chunk, size_t size)
{
	workspace data;

	if ((data = (workspace) smfi_getpriv(ctx)) == NULL)
		return smfNullWorkspaceError("filterBody");

	data->tally += size;

	if (size == 0)
		chunk = "";
	else if (size < 20)
		chunk[--size] = '\0';

	smfLog(SMF_LOG_TRACE, TAG_FORMAT "filterBody(%lx, '%.20s...', %lu) tally=%lu", TAG_ARGS, (long) ctx, chunk, (unsigned long) size, data->tally);

	return SMFIS_CONTINUE;
}

static sfsistat
filterEndMessage(SMFICTX *ctx)
{
	workspace data;
	unsigned long maxBytes;

	if ((data = (workspace) smfi_getpriv(ctx)) == NULL)
		return smfNullWorkspaceError("filterEndMessage");

	smfLog(SMF_LOG_TRACE, TAG_FORMAT "filterEndMessage(%lx)", TAG_ARGS, (long) ctx);

	maxBytes = 0 < data->maxBytesRcpt ? data->maxBytesRcpt : data->maxBytes;

	if (maxBytes != ULONG_MAX && maxBytes < data->tally)
		return smfReply(&data->work, 550, "5.3.4", "%s [%s] (%lu bytes) exceeded max. message size of %lu bytes", data->client_name, data->client_addr, data->tally, maxBytes);

	smfLog(SMF_LOG_INFO, TAG_FORMAT "messages size %lu bytes", TAG_ARGS, data->tally);

	return SMFIS_CONTINUE;
}

/*
 * Close and release per-connection resources.
 */
static sfsistat
filterClose(SMFICTX *ctx)
{
	workspace data;
	unsigned short cid = 0;

	if ((data = (workspace) smfi_getpriv(ctx)) != NULL) {
		cid = smfCloseEpilog(&data->work);
		free(data);
	}

	smfLog(SMF_LOG_TRACE, TAG_FORMAT "filterClose(%lx)", cid, smfNoQueue, (long) ctx);

	return SMFIS_CONTINUE;
}

/***********************************************************************
 ***  Milter Definition Block
 ***********************************************************************/

static smfInfo milter = {
	MILTER_MAJOR,
	MILTER_MINOR,
	MILTER_BUILD,
	MILTER_NAME,
	MILTER_VERSION,
	MILTER_COPYRIGHT,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	SMF_STDIO_CLOSE,

	/* struct smfiDesc */
	{
		MILTER_NAME,		/* filter name */
		SMFI_VERSION,		/* version code -- do not change */
		0,			/* flags */
		filterOpen,		/* connection info filter */
		NULL,			/* SMTP HELO command filter */
		filterMail,		/* envelope sender filter */
		filterRcpt,		/* envelope recipient filter */
		NULL,			/* header filter */
		NULL,			/* end of header */
		filterBody,		/* body block filter */
		filterEndMessage,	/* end of message */
		NULL,			/* message aborted */
		filterClose		/* connection cleanup */
#if SMFI_VERSION > 2
		, NULL			/* Unknown/unimplemented commands */
#endif
#if SMFI_VERSION > 3
		, NULL			/* SMTP DATA command */
#endif
	}
};

/***********************************************************************
 *** Startup
 ***********************************************************************/

static void
atExitCleanUp()
{
	smdbClose(smdbAccess);
	smfAtExitCleanUp();
}

int
main(int argc, char **argv)
{
	int argi;

	/* Default is OFF. */
	smfOptSmtpAuthOk.initial = "-";

	smfOptFile.initial = MILTER_CF;
	smfOptPidFile.initial = PID_FILE;
	smfOptRunUser.initial = RUN_AS_USER;
	smfOptRunGroup.initial = RUN_AS_GROUP;
	smfOptWorkDir.initial = WORK_DIR;
	smfOptMilterSocket.initial = "unix:" SOCKET_FILE;

	/* Parse command line options looking for a file= option. */
	optionInit(optTable, smfOptTable, NULL);
	argi = optionArrayL(argc, argv, optTable, smfOptTable, NULL);

	/* Parse the option file followed by the command line options again. */
	if (smfOptFile.string != NULL && *smfOptFile.string != '\0') {
		/* Do NOT reset this option. */
		smfOptFile.initial = smfOptFile.string;
		smfOptFile.string = NULL;

		optionInit(optTable, smfOptTable, NULL);
		(void) optionFile(smfOptFile.string, optTable, smfOptTable, NULL);
		(void) optionArrayL(argc, argv, optTable, smfOptTable, NULL);
	}

	/* Show them the funny farm. */
	if (smfOptHelp.string != NULL) {
		optionUsageL(optTable, smfOptTable, NULL);
		exit(2);
	}

	if (smfOptQuit.string != NULL) {
		/* Use SIGQUIT signal in order to avoid delays
		 * caused by libmilter's handling of SIGTERM.
		 * smfi_stop() takes too long since it waits
		 * for connections to terminate, which could
		 * be a several minutes or longer.
		 */
		exit(pidKill(smfOptPidFile.string, SIGQUIT) != 0);
	}

	if (smfOptRestart.string != NULL) {
		(void) pidKill(smfOptPidFile.string, SIGQUIT);
		sleep(2);
	}

	(void) smfi_settimeout((int) smfOptMilterTimeout.value);
	(void) smfSetLogDetail(smfOptVerbose.string);

	openlog(MILTER_NAME, LOG_PID, LOG_MAIL);

	if (smfOptDaemon.value && smfStartBackgroundProcess())
		return 1;

	if (atexit(atExitCleanUp)) {
		syslog(LOG_ERR, "atexit() failed\n");
		return 1;
	}

	if (*smfOptAccessDb.string != '\0') {
		if (smfLogDetail & SMF_LOG_DATABASE)
			smdbSetDebugMask(SMDB_DEBUG_ALL);

		if ((smdbAccess = smdbOpen(smfOptAccessDb.string, 1)) == NULL) {
			syslog(LOG_ERR, "failed to open \"%s\"", smfOptAccessDb.string);
			return 1;
		}
	}

	return smfMainStart(&milter);
}
