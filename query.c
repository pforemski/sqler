#include "common.h"

static bool handle(struct req *req, mmatic *mm)
{
	struct sqler *sqler = req->mod->cmod->prv;
	struct sreq *sreq = req->prv;

	if (mysql_query(sqler->conn, sreq->query) != 0)
		return sqlerr(-EQUERY, "SQL query failed");
	else
		uth_set_int(req->reply, "success", 1);

	/* check if we need to fetch anything back */
	MYSQL_RES *res;
	res = mysql_store_result(sqler->conn);

	if (!res) {
		/* probably an UPDATE, INSERT, etc. - fetch num of affected rows */
		uth_set_int(req->reply, "affected", mysql_affected_rows(sqler->conn));

		return true; /* we're done */
	} else {
		/* a SELECT -- fetch num of rows */
		uth_set_int(req->reply, "selected", mysql_num_rows(res));
	}

	/* fetch column names */
	MYSQL_FIELD *field;
	ut *columns = uth_set_tlist(req->reply, "columns", NULL);

	while ((field = mysql_fetch_field(res)))
		utl_add_char(columns, field->name);

	/* fetch results */
	MYSQL_ROW mrow;
	ut *row, *rows;
	unsigned int i, fields;

	fields = mysql_num_fields(res);
	rows = uth_set_tlist(req->reply, "rows", NULL);

	while ((mrow = mysql_fetch_row(res))) {
		row = utl_add_tlist(rows, NULL);

		/* FIXME: convert to related field type, basing on info in mysql_fetch_field() */
		for (i = 0; i < fields; i++) {
			if (mrow[i] == NULL)
				utl_add_char(row, "NULL");
			else
				utl_add_char(row, mrow[i]);
		}
	}

	return true;
}

struct api query_api = {
	.magic = RPCD_MAGIC,
	.handle = handle
};
