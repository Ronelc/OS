CC=g++
SRC=Container.cpp Container.h Socket.cpp Socket.h
BJS=$(SRC:.cpp=.o)
TARGETS=container sockets

TAR=tar
TARFLAGS=-cvf
TARNAME=ex5.tar
TARSRCS=$(SRC) Makefile README

all:$(TARGETS)

container: container.cpp
	$(CC) $^ -o -lstdc++fs container

sockets: sockets.cpp
	$(CC) $^ -o sockets
clean:
	$(RM) $(TARGETS) *~ *core
tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
