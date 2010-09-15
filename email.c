#include <rpcd/rpcd_module.h>

static bool handle(struct req *req)
{
	ut *ut = ut_new_thash(NULL, req);

	req->reply = rpcd_subrequest(req, "mysql", req->params);
	return true;
}

struct api email_api = {
	.tag = RPCD_TAG,
	.handle = handle
};
