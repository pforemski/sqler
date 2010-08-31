CFLAGS=-g -fPIC -lasn
MODULES=mysql.so

default: all
all: $(MODULES)

mysql.so: mysql.c
	gcc $(CFLAGS) -lmysqlclient -shared -o mysql.so mysql.c

.PHONY: clean
clean:
	-rm -f $(MODULES)
