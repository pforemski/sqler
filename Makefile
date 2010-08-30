CFLAGS=-g -fPIC -lasn -lmysqlclient
MODULES=common.so query.so

default: all
all: $(MODULES)

common.so: common.c
	gcc $(CFLAGS) -shared -o common.so common.c

query.so: query.c
	gcc $(CFLAGS) -shared -o query.so query.c

.PHONY: clean
clean:
	-rm -f $(MODULES)
