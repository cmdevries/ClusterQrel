CC = g++-4.8
INC_PATH = -I/Users/chris/boost_1_55_0
LIB_PATH = -L/Users/chris/boost_1_55_0/stage/lib
LIBS = 
CFLAGS = -std=c++0x -O2 -march=native -mtune=native $(INC_PATH)
#CFLAGS = -std=c++0x -O0 -ggdb $(INC_PATH)
LDFLAGS = $(LIB_PATH) $(LIBS)

all: cluster_qrel

cluster_qrel: cluster_qrel.cpp
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean cleanest cluster_qrel 

clean:
	rm -f *.o

cleanest: clean
	rm -f emtree
