#include <ctype.h>
#include <rpcd/rpcd_module.h>
#include "common.h"

static char *get_query(struct req *req)
{
	const char *orig_query;
	int i;
	bool inspace = false;
	xstr *query;

	query = xstr_create("", req);
	orig_query = uth_char(req->params, "query");

	i = sizeof SQLER_TAG;
	if (i > xstr_length(uth_xstr(req->params, "query")))
		goto end;

	/* skip whitechars */
	for (; orig_query[i]; i++)
		if (isalpha(orig_query[i]))
			break;

	/* replace all whitechars, newlines, etc with single space */
	for (; orig_query[i]; i++) {
		switch (orig_query[i]) {
			case ' ':
				if (inspace) continue;
				xstr_append_char(query, orig_query[i]);
				inspace = true;
				break;
			case '\t':
			case '\r':
			case '\n':
				break;
			default:
				xstr_append_char(query, orig_query[i]);
				inspace = false;
		}
	}

end:
	return xstr_string(query);
}

static char *fill_query(struct req *req, char *orig_query, tlist *data)
{
	int i, qs;
	enum fq_state { NORMAL, INQ } state = NORMAL;
	xstr *query;
	MYSQL *conn;
	ut *arg;

#define iskeyw(a) (sizeof(a) == i - qs && strncmp((a), orig_query + qs + 1, sizeof(a) - 1) == 0)
	conn = uthp_ptr(req->prv, "sqler", "conn");
	query = xstr_create("", req);
	tlist_reset(data);

	for (i = 0; orig_query[i]; i++) {
		switch (state) {
			case NORMAL:
				if (orig_query[i] == '?') {
					qs = i;
					state = INQ;
				} else {
					xstr_append_char(query, orig_query[i]);
				}
				break;

			case INQ:
				if (orig_query[i] == '?') {
					if (iskeyw("login")) {
						xstr_append(query, pb("\"%s\"", uthp_char(req->prv, "sqler", "login")));
					} else if (iskeyw("role")) {
						xstr_append(query, pb("\"%s\"", uthp_char(req->prv, "sqler", "role")));
					} else { /* probably needs an arg */
						arg = tlist_iter(data);
						if (arg) {
							if (iskeyw("int")) {
								xstr_append(query, pb("%d", ut_int(arg)));
							} else if (iskeyw("str")) {
								xstr_append(query, pb("\"%s\"", escape(conn, ut_xstr(arg))));
							} else if (iskeyw("dbl")) {
								xstr_append(query, pb("%g", ut_double(arg)));
							} else if (iskeyw("login")) {
								xstr_append(query, pb("\"%s\"", uthp_char(req->prv, "sqler", "login")));
							} else if (iskeyw("role")) {
								xstr_append(query, pb("\"%s\"", uthp_char(req->prv, "sqler", "role")));
							}

							/* XXX: arg eaten by unrecognizible substitution */
						}
					}

					state = NORMAL;
				} else if (orig_query[i] < 'a' || orig_query[i] > 'z') {
rollback:
					while (qs <= i)
						xstr_append_char(query, orig_query[qs++]);
					state = NORMAL;
				}
				break;
		}
	}

	dbg(8, "fill_query: '%s'\n", xstr_string(query));

	return xstr_string(query);
}

/****************** SQL query scanner ******************/

static void scan_file(ut *queries, const char *filepath)
{
	int i, j;
	bool inspace = false;
	char *file, *orig_query;
	xstr *query;

	file = asn_readfile(filepath, queries);
	if (!file) {
		dbg(1, "reading %s failed\n", filepath);
		return;
	}

	/* stop on each "SQLER_TAG */
	while ((file = strstr(file, "\"" SQLER_TAG))) {
		file += sizeof SQLER_TAG;

		for (i = 0; file[i]; i++) {
			/* replace \n, \t and ending \ with spaces */
			if (file[i] == '\n' || file[i] == '\t' ||
			    (file[i] == '\\' && file[i+1] == '\n')) {
				file[i] = ' ';
				continue;
			}

			if (file[i+1] == '"' && file[i] != '\\') {
				file[i+1] = '\0';
				orig_query = asn_trim(file);

				/* replace all whitechars, newlines, etc with single space */
				query = xstr_create("", queries);
				for (j = 0; orig_query[j]; j++) {
					switch (orig_query[j]) {
						case ' ':
							if (inspace) continue;
							xstr_append_char(query, orig_query[j]);
							inspace = true;
							break;
						case '\t':
						case '\r':
						case '\n':
							break;
						default:
							xstr_append_char(query, orig_query[j]);
							inspace = false;
					}
				}

				dbg(5, "%s: %s\n", filepath, xstr_string(query));
				uth_set_char(queries, xstr_string(query), filepath);

				file += i + 2;
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

/*******************************************************/

static bool init(struct mod *mod)
{
	thash *scan;
	tlist *scanlist;
	ut *v, *queries, *scandef;
	const char *rolename;
	char *path, *ext, *ast;

	/*
	 * scan source code for sql queries
	 */
	scan = uth_thash(mod->cfg, "scan");
	THASH_ITER_LOOP(scan, rolename, v) {
		/* create storage point */
		queries = uth_path_create(mod->dir->prv, "sqler", "roles", rolename, "queries");

		scanlist = ut_tlist(v);
		TLIST_ITER_LOOP(scanlist, scandef) {
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

	return true;
}

static bool handle(struct req *req)
{
	MYSQL *conn;
	thash *queries;
	char *query;

	conn = uthp_ptr(req->prv, "sqler", "conn");
	queries = uthp_thash(req->prv, "sqler", "queries");
	asnsert(conn);

	/******* check the query *******/
	query = get_query(req);
	if (!thash_get(queries, query))
		return err(-EDENY, "Access denied", query);

	/******* make the query ********/
	MYSQL_RES *res;

	query = fill_query(req, query, uth_tlist(req->params, "data"));

	if (mysql_query(conn, query) != 0)
		return sqlerr(-EQUERY, "SQL query failed");

	/* check if we need to fetch anything back */
	res = mysql_store_result(conn);

	if (!res) {
		/* probably an UPDATE, INSERT, etc. - fetch num of affected rows */
		uth_set_int(req->reply, "affected", mysql_affected_rows(conn));
		mysql_free_result(res);
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

	if (uth_bool(req->params, "verbose")) {
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

	return true;
}

struct api query_api = {
	.tag = RPCD_TAG,
	.init = init,
	.handle = handle
};

struct fw query_fw[] = {
	{ "query", true, T_STRING, NULL },
	{ "verbose", false, T_BOOL, NULL },
	{ "data", false, T_LIST, NULL },
	NULL,
};
