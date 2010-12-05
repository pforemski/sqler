/*
 * sqler - a JavaScript-MySQL bridge in C
 *
 * Copyright (C) 2010 Pawel Foremski <pawel@foremski.pl>
 * Licensed under GPLv3
 */

#include <libpjf/lib.h>
#include <rpcd/rpcd_module.h>
#include <mysql/mysql.h>
#include "common.h"

/** Construct an error reply along with MySQL error message */
bool _sqlerr(int code, const char *msg, struct req *req, const char *filename, unsigned int linenum)
{
	MYSQL *conn = uthp_ptr(req->prv, "sqler", "conn");
	asnsert(conn);
	return err(code, msg, mmatic_printf(req, "MySQL errno %u: %s", mysql_errno(conn), mysql_error(conn)));
}

bool query(MYSQL *conn, const char *query)
{
	asnsert(conn);

	if (mysql_query(conn, query) != 0)
		die("query '%s' failed: %s", query, mysql_error(conn));

	mysql_free_result(mysql_use_result(conn));
	return true;
}

MYSQL_RES *query_res(MYSQL *conn, const char *query)
{
	asnsert(conn);

	if (mysql_query(conn, query) != 0)
		die("query '%s' failed: %s", query, mysql_error(conn));

	return mysql_store_result(conn);
}

char *escape(MYSQL *conn, xstr *arg)
{
	char *buf;

	buf = mmatic_alloc(xstr_length(arg) * 2 + 1, arg);
	mysql_real_escape_string(conn, buf, xstr_string(arg), xstr_length(arg));

	return buf;
}

/****************************************************/
/************* Module implementation ****************/
/****************************************************/

static bool init(struct mod *mod)
{
	MYSQL *conn;
	const char *dbhost, *dbname, *rolename;
	thash *roles;
	tlist *scan;
	ut *dbuser, *role, *dirprv;

	dirprv = uth_path_create(mod->dir->prv, "sqler");

	/*
	 * make connections for each of cfg.roles
	 */
	dbhost = uth_char(mod->cfg, "dbhost");
	dbname = uth_char(mod->cfg, "dbname");
	roles = uth_thash(mod->cfg, "roles");

	THASH_ITER_LOOP(roles, rolename, dbuser) {
		conn = mysql_init(NULL);
		if (!conn) {
			dbg(0, "initialization of MySQL client library failed");
			return false;
		}

		if (!mysql_real_connect(conn, dbhost, uth_char(dbuser, "user"), uth_char(dbuser, "pass"), dbname, 0, NULL, 0)) {
			dbg(0, "role %s: database connection failed: %s\n", rolename, mysql_error(conn));
			return false;
		}

		query(conn, "SET NAMES 'binary'");

		dbg(8, "role %s: connected to database\n", rolename);
		role = uth_path_create(dirprv, "roles", rolename);
		uth_set_ptr(role, "conn", conn);
	}

	/*
	 * prepare database
	 */
	conn = uthp_ptr(dirprv, "roles", "admin", "conn");
	if (!conn) {
		dbg(0, "missing MySQL connection for 'admin' role\n");
		return false;
	}

	/* create table users -- if not exists */
	if (!query(conn, SQLER_USERS_TABLE))
		return false;

	/* create table sessions -- if not exists */
	if (!query(conn, SQLER_SESSIONS_TABLE))
		return false;

	/* drop old sessions */
	if (!query(conn, "DELETE FROM sessions WHERE timestamp < UNIX_TIMESTAMP() - " SQLER_SESSION_TIMEOUT))
		return false;

	return true;
}

static bool handle(struct req *req)
{
	const char *session, *role, *login;
	ut *dirprv, *reqprv;
	thash *queries;
	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;

	/* skip for "login" */
	if (streq(req->method, "login"))
		return true;

	dirprv = uth_path_create(req->mod->dir->prv, "sqler");
	reqprv = uth_path_create(req->prv, "sqler");
	conn = uthp_ptr(dirprv, "roles", "admin", "conn");
	asnsert(dirprv && reqprv && conn);

	/* session is required for all other methods */
	session = uth_char(req->params, "session");
	if (!session)
		return err(-ENOSESS, "Session ID required", NULL);

	/* get session login and role */
	res = query_res(conn, pb(
		"SELECT login, role FROM sessions \
		WHERE id='%s' AND timestamp >= UNIX_TIMESTAMP() - " SQLER_SESSION_TIMEOUT " LIMIT 1",
		session));

	if (!(row = mysql_fetch_row(res))) {
		mysql_free_result(res);
		return err(-ESESS, "Session not found", session);
	}

	login = mmatic_strdup(row[0], req);
	role = mmatic_strdup(row[1], req);
	mysql_free_result(res);

	/* update session */
	query(conn, pb("UPDATE sessions SET timestamp = UNIX_TIMESTAMP() WHERE id='%s'", session));

	/* copy to req data */
	uth_set_char(reqprv, "role", role);
	uth_set_char(reqprv, "login", login);

	uth_set_ptr(reqprv, "conn", uthp_ptr(dirprv, "roles", role, "conn"));

	queries = uthp_thash(dirprv, "roles", role, "queries");
	if (queries)
		uth_set_thash(reqprv, "queries", queries);

	if (!uth_get(reqprv, "conn"))
		return err(-ECONN, "DB connection not found for given role", role);

	return true;
}

struct api common_api = {
	.tag = RPCD_TAG,
	.init = init,
	.handle = handle
};

struct fw common_fw[] = {
	{ "session", false, T_STRING, "/^[a-z0-9A-Z]+$/" },
	NULL,
};
