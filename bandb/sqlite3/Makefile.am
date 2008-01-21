

SOURCES = sqlite3.c

.c.lo:
	$(LIBTOOL) --mode=compile $(CC) $(CFLAGS) -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 -prefer-pic -I../include -I. -c $< -o $@ 

S_OBJS= $(SOURCES:.c=.lo)

libsqlite3.a: $(S_OBJS)
	$(LIBTOOL) --mode=link $(CC) -all-static -o libsqlite3.a $(S_OBJS)

all: libsqlite3.a
