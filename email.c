#include <rpcd/rpcd_module.h>
#include <libesmtp.h>
#include <auth-client.h>
#include "common.h"

#define pb(...) mmatic_printf(req, __VA_ARGS__)

static bool smtperr(int code, char *msg, struct req *req)
{
	char buf[128];

	return err(code, msg, smtp_strerror(smtp_errno(), buf, sizeof buf));
}

static int auth_cb(auth_client_request_t request, char **result, int fields, void *arg)
{
	struct req *req = arg;
	int i;

	for (i = 0; i < fields; i++) {
		if (request[i].flags & AUTH_USER)
			result[i] = mmatic_strdup(uth_char(req->mod->cfg, "user"), req);
		else if (request[i].flags & AUTH_PASS)
			result[i] = mmatic_strdup(uth_char(req->mod->cfg, "password"), req);
	}

	return 1;
}

static const char *readmsg_cb(void **buf, int *len, void *arg)
{
	struct req *req = arg;
	ut *prv;
	int state;

	prv = uth_path_create(req->prv, "sqler");

	if (len) {
		state = uth_int(prv, "readmsg_cb"); /* defaults to 0 */
		uth_set_int(prv, "readmsg_cb", state+1);

		switch (state) {
			case 0:
				*len = 2;
				return "\r\n";
			case 1:
				*len = xstr_length(uth_xstr(req->params, "body"));
				return uth_char(req->params, "body");
			default:
				*len = 0;
		}
	}

	return NULL;
}

static bool sendmail(struct req *req)
{
	smtp_session_t session;
	smtp_message_t message;
	smtp_recipient_t recipient;
	auth_context_t authctx;
	const smtp_status_t *status;
	const char *host, *port, *subject;

	session = smtp_create_session();
	message = smtp_add_message(session);

	auth_client_init();
	authctx = auth_create_context();

	/* SMTP server */
	host = uth_char(req->mod->cfg, "host");
	port = uth_char(req->mod->cfg, "port");
	smtp_set_server(session, pb("%s:%s", host ? host : "localhost", port ? port : "25"));

	/* SMTP auth */
	if (uth_char(req->mod->cfg, "user") && uth_char(req->mod->cfg, "password")) {
		auth_set_mechanism_flags(authctx, AUTH_PLUGIN_PLAIN, 0);
		auth_set_interact_cb(authctx, auth_cb, (void *) req);
		smtp_auth_set_context(session, authctx);
	}

	/* SMTP sender */
	smtp_set_header(message, "From", uth_char(req->params, "from_name"), uth_char(req->params, "from"));
	smtp_set_header_option(message, "From", Hdr_OVERRIDE, 1);
	smtp_set_reverse_path(message, uth_char(req->params, "from"));

	/* SMTP recipient */
	smtp_set_header(message, "To", uth_char(req->params, "to_name"), uth_char(req->params, "to"));
	smtp_set_header_option(message, "To", Hdr_OVERRIDE, 1);
	smtp_add_recipient(message, uth_char(req->params, "to"));

	/* SMTP subject */
	subject = uth_char(req->params, "subject");
	if (subject) {
		smtp_set_header(message, "Subject", subject);
		smtp_set_header_option(message, "Subject", Hdr_OVERRIDE, 1);
	}

	/* SMTP message body */
	smtp_set_messagecb(message, readmsg_cb, (void *) req);

	/* sail! */
	if (!smtp_start_session(session)) {
		smtperr(-EEMAILSESS, "SMTP session failed", req);
		goto end;
	}

	status = smtp_message_transfer_status(message);
	if (!status) {
		err(-EEMAILNOSTATUS, "Could not retrieve SMTP message transfer status", NULL);
		goto end;
	}

	if (status->code != 250) {
		err(status->code, "SMTP error", mmatic_strdup(status->text, req));
		goto end;
	}

	/* success */
	uth_set_char(req->reply, "status", status->text);

end:
	smtp_destroy_session(session);
	auth_destroy_context(authctx);
	auth_client_exit();
	return ut_ok(req->reply);
}

/*****************************************/

static bool handle(struct req *req)
{
	const char *limit, *login, *at;
	tlist *unlimit;
	ut *prv, *v;

	prv = uth_path_create(req->prv, "sqler");

	/*
	 * handle "limit-domain"
	 */
	login = uth_char(prv, "login");
	limit = uth_char(req->mod->cfg, "limit-domain");
	if (limit) {
		/* if in "unlimit", allow */
		unlimit = uth_tlist(req->mod->cfg, "unlimit");
		TLIST_ITER_LOOP(unlimit, v) {
			if (streq(login, ut_char(v))) {
				dbg(1, "user '%s' allowed to send anywhere\n", login);
				goto auth_ok;
			}
		}

		/* otherwise check recipient */
		at = strchr(uth_char(req->params, "to"), '@');
		asnsert(at); /* guaranteed by email_fw regexp */

		if (!streq(at+1, limit))
			return err(-EEMAILLIMIT, "Tried to send outside of domain limit", limit);
	}

auth_ok:
	sendmail(req);
	return true;
}

struct api email_api = {
	.tag = RPCD_TAG,
	.handle = handle
};

struct fw email_fw[] = {
	{ "from", true, T_STRING, "/[^ ]+@[^ ]+\\.[a-z]+$/" },  /* email of sender */
	{   "to", true, T_STRING, "/[^ ]+@[^ ]+\\.[a-z]+$/" },  /* email of recipient */
	{ "body", true, T_STRING, NULL },                       /* message body */

	{ "subject", false, T_STRING, NULL },                   /* message subject */
	{ "from_name", false, T_STRING, NULL },                 /* name of sender */
	{ "to_name", false, T_STRING, NULL },                   /* name of recipient */
	NULL,
};
