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

	for (i = 0; orig_query[i]; i++)
		if (isalpha(orig_query[i]))
			break;

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
	.handle = handle
};

struct fw query_fw[] = {
	{ "query", true, T_STRING, NULL },
	{ "verbose", false, T_BOOL, NULL },
	{ "data", false, T_LIST, NULL },
	NULL,
};
