all: mipc_test

mipc_test: mipc_test.c inc.h
	cc -o $@ $< -lsys

test_subscriber: test_subscriber.c manager.c
	cc -o $@ test_subscriber.c manager.c


run: all
	./mipc_test

clean:
	rm -f mipc_test

