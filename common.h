#ifndef _COMMON_H_
#define _COMMON_H_

#include <libasn/lib.h>
#include <mysql/mysql.h>

#define SQLER_DEFAULT_EXT "js"
#define SQLER_TAG "sqler:"
#define SQLER_SESSION \
	"CREATE TABLE IF NOT EXISTS session ("                \
	"  id        varchar(255) NOT NULL,"                  \
	"  data      varchar(255),"                           \
	"  timestamp int(10) unsigned NOT NULL default '0',"  \
	"  PRIMARY KEY (id)"                                  \
	") ENGINE=MEMORY"

/** Errors */
#define EDBUSER 1
#define EDBNAME 2
#define EDBCONN 3
#define EDBQUERY 4
#define EQUERY 5
#define EINIT 6

/****************************************************/
/**************** Library functions *****************/
/****************************************************/

/** Construct an error reply along with MySQL error message */
bool _sqlerr(int code, const char *msg, struct req *req, const char *filename, unsigned int linenum);

#define sqlerr(code, msg) _sqlerr(code, msg, uth_ptr(req->prv, "sqler.conn"), __FILE__, __LINE__)

#endif
