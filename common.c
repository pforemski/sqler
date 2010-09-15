#include <libasn/lib.h>
#include <rpcd/rpcd_module.h>
#include <mysql/mysql.h>
#include "common.h"

#define pm(...) mmatic_printf(mod, __VA_ARGS__)
#define pr(...) mmatic_printf(req, __VA_ARGS__)

/****************************************************/
/**************** Library functions *****************/
/****************************************************/

/** Construct an error reply along with MySQL error message */
bool _sqlerr(int code, const char *msg, struct req *req, const char *filename, unsigned int linenum)
{
	MYSQL *conn = uth_ptr(req->prv, "sqler.conn");
	asnsert(conn);
	return err(code, msg, mmatic_printf(req, "MySQL errno %u: %s", mysql_errno(conn), mysql_error(conn)));
}

#if 0
/** Parse db connection params and check if we have a query */
bool parse_query(struct req *req)
{
	struct sreq *sreq = uth_ptr(req->prv, "sqler.sreq");
	const char *s;

	if ((s = uth_char(req->mod->cfg, "host")))
		sreq->host = s;
	else
		sreq->host = "localhost";

	if ((s = uth_char(req->mod->cfg, "user"))) {
		sreq->user = s;
	} else if ((s = uth_char(req->params, "user"))) {
		if (asn_match("/^[a-z0-9]+$/", s))
			sreq->user = s;
		else
			return err(-EDBUSER, "Invalid characters in database user", s);
	}

	if ((s = uth_char(req->mod->cfg, "pass"))) {
		sreq->pass = s;
	} else if ((s = uth_char(req->params, "pass"))) {
		sreq->pass = s;
	}

	if ((s = uth_char(req->mod->cfg, "db"))) {
		sreq->db = s;
	} else if ((s = uth_char(req->params, "db"))) {
		if (asn_match("/^[A-Za-z0-9_-]+$/", s))
			sreq->db = s;
		else
			return err(-EDBNAME, "Invalid characters in database name", s);
	}

	/* check if we have a query :) */
	if ((s = uth_char(req->mod->cfg, "query"))) {
		sreq->query = s;
	} else if ((s = uth_char(req->params, "query"))) {
		sreq->query = s;
	} else {
		return err(-EDBQUERY, "No SQL query given", NULL);
	}

	/* check requested reply format */
	if ((s = uth_char(req->params, "compact")))
		sreq->repmode = REPLY_COMPACT;
	else
		sreq->repmode = REPLY_VERBOSE;

	return true;
}

#endif

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

static bool query(MYSQL *conn, const char *query, const char *msg)
{
	MYSQL_RES *res;

	if (mysql_query(conn, query) != 0) {
		dbg(0, "%s failed: %s\n", msg, mysql_error(conn));
		return false;
	}

	res = mysql_use_result(conn);
	mysql_free_result(res);

	return true;
}

/****************************************************/
/************* Module implementation ****************/
/****************************************************/

static bool init(struct mod *mod)
{
	const char *dbhost;
	const char *dbname;
	thash *dbusers;
	const char *rolename;
	ut *dbuser, *scandef;
	MYSQL *conn;
	tlist *scan;
	char *path, *ext, *ast;
	ut *queries;

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
		uth_set_ptr(mod->dir->prv, pm("sqler.common.role.%s.conn", rolename), conn);

		/*
		 * scan source code for sql queries
		 */
		scan = uth_tlist(dbuser, "scan");
		if (scan) {
			queries = uth_set_thash(mod->dir->prv, pm("sqler.common.role.%s.queries", rolename), NULL);

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
	 * check database, prepare sessions
	 */
	conn = uth_ptr(mod->dir->prv, "sqler.common.role.admin.conn");
	if (!conn) {
		dbg(0, "missing MySQL connection for 'admin' role");
		return false;
	}

	/* sqler: describe users -- check if table exists */
	if (!query(conn, "describe users", "check for 'users' table"))
		return false;

	/* sqler: create table sessions -- if not exists */
	if (!query(conn, SQLER_SESSION, "creation of 'session' table"))
		return false;

	return true;
}

static bool handle(struct req *req)
{
	/* skip for "login" */

	/* find session and set req->sqler.common.conn and .queries */

#if 0
	/******* make the query ********/

	if (mysql_query(sreq->conn, sreq->query) != 0)
		return sqlerr(-EQUERY, "SQL query failed");

	/* check if we need to fetch anything back */
	MYSQL_RES *res;
	res = mysql_store_result(sreq->conn);

	if (!res) {
		/* probably an UPDATE, INSERT, etc. - fetch num of affected rows */
		uth_set_int(req->reply, "affected", mysql_affected_rows(sreq->conn));

		return true; /* we're done */
	} else {
		/* a SELECT -- fetch num of rows */
		uth_set_int(req->reply, "rowcount", mysql_num_rows(res));
	}

	/******* fetch the results ********/

	MYSQL_FIELD *fields;
	MYSQL_ROW mrow;
	ut *row, *rows, *columns;
	unsigned int i, num;

	rows = uth_set_tlist(req->reply, "rows", NULL);
	fields = mysql_fetch_fields(res);
	num = mysql_num_fields(res);

	if (sreq->repmode == REPLY_VERBOSE) {
		while ((mrow = mysql_fetch_row(res))) {
			row = utl_add_thash(rows, NULL);

			for (i = 0; i < num; i++) {
				if (mrow[i] == NULL)
					uth_set_null(row, fields[i].name);
				else
					uth_set_char(row, fields[i].name, mrow[i]);
			}
		}
	} else {
		ut *columns = uth_set_tlist(req->reply, "columns", NULL);

		for (i = 0; i < num; i++)
			utl_add_char(columns, fields[i].name);

		while ((mrow = mysql_fetch_row(res))) {
			row = utl_add_tlist(rows, NULL);

			for (i = 0; i < num; i++) {
				if (mrow[i] == NULL)
					utl_add_null(row);
				else
					utl_add_char(row, mrow[i]);
			}
		}
	}
#endif

	return true;
}

struct api common_api = {
	.tag = RPCD_TAG,
	.init = init,
	.handle = handle
};
