CC=g++
RANLIB=ranlib

LIBSRC=MapReduceFramework.cpp
LIBOBJ=$(LIBSRC:.cpp=.o)

INCS=-I.
CPPFLAGS = -std=c++11 -Wall -Wextra -pthread -g $(INCS)
LOADLIBES = -L./ 

MAPREDUCELIB = MapReduceFramework.a
TARGETS = $(MAPREDUCELIB)

TAR=tar
TARFLAGS=-cvf
TARNAME=ex3.tar
TARSRCS=$(LIBSRC) Search.cpp Makefile README FCFSGanttChart.jpg \
	PSGanttChart.jpg RRGanttChart.jpg SRTFGanttChart.jpg

all: $(TARGETS) Search

$(TARGETS): $(LIBOBJ)
	$(AR) $(ARFLAGS) $@ $^
	$(RANLIB) $@

Search: Search.o
	$(CC) $(CPPFLAGS) Search.o $(MAPREDUCELIB) -o Search

Search.o: Search.cpp
	$(CC) $(CPPFLAGS) -c Search.cpp

clean:
	$(RM) $(TARGETS) $(MAPREDUCELIB) Search Search.o $(OBJ) $(LIBOBJ) *~ *core


depend:
	makedepend -- $(CPPFLAGS) -- $(SRC) $(LIBSRC)


tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
