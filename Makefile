CFLAGS=-g -fPIC -lasn
MODULES=mysql.so email.so

default: all
all: $(MODULES)

mysql.so: mysql.c
	gcc $(CFLAGS) -lmysqlclient -shared -o mysql.so mysql.c

email.so: email.c
	gcc $(CFLAGS) -shared -o email.so email.c

.PHONY: clean
clean:
	-rm -f $(MODULES)
