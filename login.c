#include <time.h>
#include <rpcd/rpcd_module.h>
#include "common.h"

static bool init(struct mod *mod)
{
	srandom(time(0));
	return true;
}

static bool handle(struct req *req)
{
	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;
	xstr *xs;
	const char *slogin, *spass, *sess;

	conn = uthp_ptr(req->mod->dir->prv, "sqler", "roles", "admin", "conn");
	asnsert(conn);

	slogin = escape(conn, uth_xstr(req->params, "login"));
	spass  = escape(conn, uth_xstr(req->params, "password"));

	res = query_res(conn, pb(
		"SELECT role FROM users WHERE login=\"%s\" AND password=\"%s\"",
		slogin, spass));

	row = mysql_fetch_row(res);
	if (!row)
		return err(-ELOGIN, "Login failed", "");

	/* TODO: could be better */
	xs = xstr_create(uth_char(req->params, "login"), req);
	xstr_append(xs, pb("%u", random()));
	sess = asn_b32_enc(xs, req);

	query(conn, pb(
		"UPDATE sessions SET id=\"%s\", login=\"%s\", role=\"%s\", timestamp=UNIX_TIMESTAMP()",
		sess, slogin, row[0]));

	uth_set_char(req->reply, "session", sess);
	return true;
}

struct api login_api = {
	.tag = RPCD_TAG,
	.init = init,
	.handle = handle
};

struct fw login_fw[] = {
	{ "login", true, T_STRING, NULL },
	{ "password", true, T_STRING, NULL },
	NULL,
};
