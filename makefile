bhtmake:	bht.c
	gcc -pthread -g -o bht bht.c

clean:
	rm bht