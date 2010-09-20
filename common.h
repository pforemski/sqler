#ifndef _COMMON_H_
#define _COMMON_H_

#include <libasn/lib.h>
#include <mysql/mysql.h>

#define SQLER_DEFAULT_EXT "js"
#define SQLER_TAG "sqler:"

#define SQLER_USERS_TABLE \
	"CREATE TABLE IF NOT EXISTS users ("                  \
	"  id        int unsigned NOT NULL AUTO_INCREMENT,"   \
	"  login     varchar(255),"                           \
	"  password  varchar(255),"                           \
	"  role      varchar(255),"                           \
	"  PRIMARY KEY (id))"

#define SQLER_SESSIONS_TABLE \
	"CREATE TABLE IF NOT EXISTS sessions ("               \
	"  id        varchar(255) NOT NULL,"                  \
	"  login     varchar(255),"                           \
	"  role      varchar(255),"                           \
	"  timestamp int(10) unsigned NOT NULL default '0',"  \
	"  PRIMARY KEY (id))"

/** Errors */
#define ESESS 1
#define ECONN 2
#define ENOSESS 3
#define EQUERY 4
#define EDENY 5
#define ELOGIN 6
#define EEMAILSESS 7
#define EEMAILNOSTATUS 8
#define EEMAILSTATUS 9
#define EEMAILLIMIT 10

/****************************************************/
/**************** Library functions *****************/
/****************************************************/

/** Construct an error reply along with MySQL error message */
bool _sqlerr(int code, const char *msg, struct req *req, const char *filename, unsigned int linenum);
#define sqlerr(code, msg) _sqlerr(code, msg, req, __FILE__, __LINE__)

/** Make a MySQL query, die if it fails and do not return any data */
bool query(MYSQL *conn, const char *query);

/** Make a MySQL query, die if it fails and do return mysql res
 * @note remember about mysql_free_result() */
MYSQL_RES *query_res(MYSQL *conn, const char *query);

/** Escape given string using mysql_real_escape_string() */
char *escape(MYSQL *conn, xstr *arg);

#define pb(...) mmatic_printf(req, __VA_ARGS__)

#endif
