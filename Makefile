all : site_unblock

site_unblock: main.o
	g++ -g -o site_unblock main.o -pthread

main.o:
	g++ -g -c -o main.o main.cpp

clean:
	rm -f site_unblock
	rm -f *.o

