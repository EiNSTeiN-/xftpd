
GCC=gcc -Os -s -Wall
RM=rm -f

# needed libraries dor different systems.
LIBS_BASE=
LIBS_BASE_MINGW=-lws2_32 -lgdi32
LIBS_MASTER=-lssl -lcrypto -ltolua++ -llua -lz -lm $(LIBS_BASE) 
LIBS_SLAVE=-lssl -lcrypto -lz $(LIBS_BASE) 
LIBS_PROXY=$(LIBS_BASE)

# file extention for executable files
EXE=

XFTPD_OBJECTS =  asprintf.o base64.o config.o crypto.o io.o logging.o main.o \
				 socket.o collection.o ftpd.o vfs.o slaves.o time.o events.o \
				 luainit.o luacollection.o luaconfig.o luaevents.o luairc.o \
				 luamirror.o luasite.o luatimer.o luavfs.o luaskins.o \
				 scripts.o irccore.o tree.o users.o sfv.o stats.o \
				 slaveselection.o timer.o mirror.o packet.o site.o signal.o \
				 nuke.o service.o asynch.o obj.o crc32.o update.o \
				 blowfish.o secure.o adio.o skins.o dir.o wild.o

SLAVE_OBJECTS =  asprintf.o base64.o config.o crypto.o io.o logging.o socket.o \
				 collection.o fsd.o time.o crc32.o service.o signal.o packet.o \
				 obj.o adio.o secure.o dir.o wild.o

PROXY_OBJECTS =  socket.o collection.o packet.o proxy.o signal.o config.o \
				 logging.o asprintf.o service.o time.o obj.o crc32.o adio.o

LUABIND_OBJECTS = xFTPd_bind.o

all:
	@echo Try \"make mingw\" or \"make generic\".

generic: proxy/proxy$(EXE) master/xFTPd$(EXE) slave/slave$(EXE)

mingw:
	make generic "LIBS_BASE=$(LIBS_BASE) $(LIBS_BASE_MINGW)" "EXE=.exe"

.c.o:
	$(GCC) -c $*.c

xFTPd_bind.c: xFTPd_bind.pkg
	tolua++ -n xftpd -o $@ $<

master/xFTPd$(EXE): $(LUABIND_OBJECTS) $(XFTPD_OBJECTS)
	$(GCC) -o master/xFTPd$(EXE) $(LUABIND_OBJECTS) $(XFTPD_OBJECTS) $(LIBS_MASTER)

slave/slave$(EXE): $(SLAVE_OBJECTS)
	$(GCC) -o slave/slave$(EXE) $(SLAVE_OBJECTS) $(LIBS_SLAVE)

proxy/proxy$(EXE): $(PROXY_OBJECTS)
	$(GCC) -o proxy/proxy$(EXE) $(PROXY_OBJECTS) $(LIBS_PROXY)

clean:
	$(RM) master/xFTPd$(EXE) slave/slave$(EXE) proxy/proxy$(EXE) xFTPd_bind.c *.o
