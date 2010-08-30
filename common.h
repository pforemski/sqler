#include <libasn/lib.h>
#include <rpcd/rpcd.h>
#include <mysql/mysql.h>

#define EDBUSER 1
#define EDBNAME 2
#define EDBCONN 3
#define EDBQUERY 4
#define EQUERY 5

/** Represents module instance */
struct sqler {
	MYSQL *conn;         /**> MySQL connection */
};

/** Represents single request */
struct sreq {
	/* XXX: all variables may be NULL */
	const char *host;
	const char *user;
	const char *pass;
	const char *db;

	const char *query;
};

/** Handle MySQL error */
#define sqlerr(code, msg) _sqlerr(code, msg, req, __FILE__, __LINE__)
bool _sqlerr(int code, const char *msg, struct req *req,
	const char *filename, unsigned int linenum);
