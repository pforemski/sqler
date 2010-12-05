CFLAGS=-g -fPIC -lpjf
MODULES=common.so query.so email.so login.so

default: all
all: $(MODULES)

common.so: common.c
	gcc $(CFLAGS) -lmysqlclient -shared -o common.so common.c

query.so: query.c
	gcc $(CFLAGS) -lmysqlclient -shared -o query.so query.c

email.so: email.c
	gcc $(CFLAGS) -lesmtp -shared -o email.so email.c

login.so: login.c
	gcc $(CFLAGS) -shared -o login.so login.c

.PHONY: clean
clean:
	-rm -f $(MODULES)
