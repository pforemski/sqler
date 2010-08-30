#include <libasn/lib.h>
#include <rpcd/rpcd.h>
#include <mysql/mysql.h>
#include "common.h"

/*** Library functions ***/
bool _sqlerr(int code, const char *msg, struct req *req,
	const char *filename, unsigned int linenum)
{
	struct sqler *sqler = (struct sqler *) req->mod->cmod->prv;
	struct mmatic *mm = req->mm;

	return err(code, msg,
		pbt("MySQL errno %u: %s", mysql_errno(sqler->conn), mysql_error(sqler->conn)));
}

/******************************************************/

static bool init(struct mod *mod)
{
	struct sqler *sqler;

	sqler = mmzalloc(sizeof(struct sqler));
	mod->cmod->prv = sqler;

	sqler->conn = mysql_init(NULL);

	return true;
}

static bool handle(struct req *req, mmatic *mm)
{
	struct sqler *sqler = (struct sqler *) req->mod->cmod->prv;
	struct sreq *sreq;

	sreq = mmzalloc(sizeof(struct sreq));
	req->prv = sreq;

	/* parse connection info
	 * XXX: these values may be NULL in order to use MySQL defaults */
	{
		const char *s;

		sreq->host = "localhost";

		s = uth_char(req->query, "user");
		if (s) {
			if (asn_match("/^[A-Za-z0-9]+$/", s))
				sreq->user = s;
			else
				return err(-EDBUSER, "Invalid characters in database user", s);
		}

		sreq->pass = uth_char(req->query, "pass");

		s = uth_char(req->query, "db");
		if (s) {
			if (asn_match("/^[A-Za-z0-9_-]+$/", s))
				sreq->db = s;
			else
				return err(-EDBNAME, "Invalid characters in database name", s);
		}
	}

	/* check if we have a query :) */
	{
		const char *s;

		s = uth_char(req->query, "query");
		if (!s)
			return err(-EDBQUERY, "No SQL query given", NULL);
		else
			sreq->query = s;
	}

	/* connect */
	if (!mysql_real_connect(sqler->conn, sreq->host, sreq->user, sreq->pass, sreq->db, 0, NULL, 0))
		return sqlerr(-EDBCONN, "Database connection failed");
	else
		dbg(3, "connected to database\n");

	return true;
}

struct api common_api = {
	.magic = RPCD_MAGIC,
	.init = init,
	.handle = handle
};
