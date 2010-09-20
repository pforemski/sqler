#include <libasn/lib.h>
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

static void scan_file(ut *queries, const char *filepath)
{
	int i;
	char *file, *query;

	file = asn_readfile(filepath, queries);
	if (!file) {
		dbg(1, "reading %s failed\n", filepath);
		return;
	}

	while ((file = strstr(file, "\"" SQLER_TAG))) {
		file += sizeof SQLER_TAG;

		for (i = 0; file[i]; i++) {
			if (file[i] == '\n' || file[i] == '\t') {
				file[i] = ' ';
				continue;
			}

			if (file[i] == '"' && file[i-1] != '\\') {
				file[i] = '\0';
				query = asn_trim(file);

				dbg(5, "%s: %s\n", filepath, query);
				uth_set_char(queries, query, filepath);

				file += i + 1;
				break;
			}
		}
	}
}

static void scan_dir(ut *queries, const char *dirpath, const char *ext)
{
	tlist *ls;
	const char *name, *path, *dot;

	ls = asn_ls(dirpath, queries);
	TLIST_ITER_LOOP(ls, name) {
		path = mmatic_printf(queries, "%s/%s", dirpath, name);

		if (asn_isdir(path) == 1) {
			scan_dir(queries, path, ext);
			continue;
		}

		dot = strchr(name, '.');
		if (!dot) continue;
		if (!streq(dot + 1, ext)) continue;

		scan_file(queries, path);
	}
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
	MYSQL_RES *res;
	MYSQL_ROW row;
	const char *dbhost, *dbname, *rolename;
	thash *dbusers;
	tlist *scan;
	char *path, *ext, *ast;
	ut *dbuser, *scandef, *queries, *role, *dirprv, *session;

	dirprv = uth_path_create(mod->dir->prv, "sqler");

	/*
	 * make connections for each of cfg.dbusers{user,pass,scan}
	 */
	dbhost = uth_char(mod->cfg, "dbhost");
	dbname = uth_char(mod->cfg, "dbname");
	dbusers = uth_thash(mod->cfg, "dbusers");

	THASH_ITER_LOOP(dbusers, rolename, dbuser) {
		conn = mysql_init(NULL);
		if (!conn) {
			dbg(0, "initialization of MySQL client library failed");
			return false;
		}

		if (!mysql_real_connect(conn, dbhost, uth_char(dbuser, "user"), uth_char(dbuser, "pass"), dbname, 0, NULL, 0)) {
			dbg(0, "role %s: database connection failed: %s\n", rolename, mysql_error(conn));
			return false;
		}

		dbg(3, "role %s: connected to database\n", rolename);
		role = uth_path_create(dirprv, "roles", rolename);
		uth_set_ptr(role, "conn", conn);

		/*
		 * scan source code for sql queries
		 */
		scan = uth_tlist(dbuser, "scan");
		if (scan) {
			queries = uth_set_thash(role, "queries", NULL);

			TLIST_ITER_LOOP(scan, scandef) {
				/* unconst */
				path = mmatic_strdup(ut_char(scandef), mod);

				/* get dir path and file extension */
				ext = NULL;
				ast = strchr(path, '*');
				if (ast) {
					*ast = '\0';
					if (ast[1] == '.')
						ext = ast + 2;
					else
						ext = ast + 1;
				}

				if (!ext || !ext[0])
					ext = SQLER_DEFAULT_EXT;

				scan_dir(queries, path, ext);
			}
		}
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
	if (!query(conn, "DELETE FROM sessions WHERE timestamp < UNIX_TIMESTAMP() - 3600 * 24 * 7"))
		return false;

	/*
	 * read all sessions
	 */
	res = query_res(conn, "SELECT id, login, role FROM sessions");
	while ((row = mysql_fetch_row(res))) {
		dbg(5, "loading session '%s': login '%s', role '%s'\n", row[0], row[1], row[2]);

		session = uth_path_create(dirprv, "sessions", row[0]);
		uth_set_char(session, "login", row[1]);
		uth_set_char(session, "role", row[2]);
	}
	mysql_free_result(res);

	return true;
}

static bool handle(struct req *req)
{
	const char *user_session, *role, *login;
	ut *dirprv, *reqprv;

	/* skip for "login" */
	if (streq(req->method, "login"))
		return true;

	/* check session */
	user_session = uth_char(req->params, "session");
	if (!user_session)
		return err(-ENOSESS, "Session ID required", NULL);

	dirprv = uth_path_create(req->mod->dir->prv, "sqler");
	role  = uthp_char(dirprv, "sessions", user_session, "role");
	login = uthp_char(dirprv, "sessions", user_session, "login");

	if (!(role && login))
		return err(-ESESS, "Session not found", user_session);

	/* TODO: update session, make use of timestamp, etc. */

	/* copy to req data */
	reqprv = uth_path_create(req->prv, "sqler");

	uth_set_char(reqprv, "role", role);
	uth_set_char(reqprv, "login", login);

	uth_set_ptr(reqprv, "conn", uthp_ptr(dirprv, "roles", role, "conn"));
	uth_set_thash(reqprv, "queries", uthp_thash(dirprv, "roles", role, "queries"));

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
