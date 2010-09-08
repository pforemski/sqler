#include <libasn/lib.h>
#include <rpcd/rpcd.h>
#include <mysql/mysql.h>

/****************************************************/
/****************** Defines *************************/
/****************************************************/

/** Errors */
#define EDBUSER 1
#define EDBNAME 2
#define EDBCONN 3
#define EDBQUERY 4
#define EQUERY 5
#define EINIT 6

/** Represents request data */
struct sreq {
	MYSQL *conn;
	const char *query;
	enum reply_format {
		REPLY_VERBOSE = 1,
		REPLY_COMPACT
	} repmode;

	/* XXX: these may be NULL */
	const char *host;
	const char *user;
	const char *pass;
	const char *db;
};

/** Handle MySQL error, with given code and message string */
#define sqlerr(code, msg) _sqlerr(code, msg, req, __FILE__, __LINE__)

/****************************************************/
/**************** Library functions *****************/
/****************************************************/

/** Construct an error reply along with MySQL error message */
bool _sqlerr(int code, const char *msg, struct req *req,
	const char *filename, unsigned int linenum)
{
	struct sreq *sreq = req->prv;
	struct mmatic *mm = req->mm;

	return err(code, msg,
		pbt("MySQL errno %u: %s", mysql_errno(sreq->conn), mysql_error(sreq->conn)));
}

/** Parse db connection params and check if we have a query */
bool parse_query(struct req *req)
{
	struct sreq *sreq = req->prv;
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

/** Open DB connection using user-supplied data */
bool db_connect(struct req *req)
{
	struct sreq *sreq = req->prv;

	sreq->conn = mysql_init(NULL);
	if (!sreq->conn)
		return err(-EINIT, "Initialization of MySQL client library failed", NULL);

	if (!mysql_real_connect(sreq->conn, sreq->host, sreq->user, sreq->pass, sreq->db, 0, NULL, 0)) {
		dbg(3, "database connection failed\n");
		return sqlerr(-EDBCONN, "Database connection failed");
	} else {
		dbg(3, "connected to database\n");
	}

	return true;
}

bool db_disconnect(struct req *req)
{
	struct sreq *sreq = req->prv;

	mysql_close(sreq->conn);

	return true;
}

/****************************************************/
/************* Module implementation ****************/
/****************************************************/

static bool handle(struct req *req, mmatic *mm)
{
	struct sreq *sreq;

	/******* initialization stuff ********/

	sreq = mmzalloc(sizeof(struct sreq));
	req->prv = sreq;

	if (!parse_query(req))
		return false;

	if (!db_connect(req))
		return false;

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

	db_disconnect(req);

	return true;
}

struct api mysql_api = {
	.magic = RPCD_MAGIC,
	.handle = handle
};
