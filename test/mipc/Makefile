all: mipc_test

mipc_test: mipc_test.c inc.h
	cc -o $@ $< -lsys

run: all
	./mipc_test

clean:
	rm -f mipc_test

