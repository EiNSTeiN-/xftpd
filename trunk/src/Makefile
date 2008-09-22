include vars.mk

XFTPD_OBJECTS =  asprintf.o base64.o config.o crypto.o io.o logging.o main.o \
				 socket.o collection.o ftpd.o vfs.o slaves.o time.o events.o \
				 luainit.o scripts.o irccore.o tree.o users.o sfv.o stats.o \
				 slaveselection.o timeout.o mirror.o packet.o site.o signal.o \
				 nuke.o service.o asynch.o obj.o crc32.o update.o lsqlite3.o

SLAVE_OBJECTS =  asprintf.o base64.o config.o crypto.o io.o logging.o socket.o \
				 collection.o fsd.o time.o crc32.o service.o signal.o packet.o \
				 obj.o adio.o

PROXY_OBJECTS =  socket.o collection.o packet.o proxy.o signal.o config.o \
				 logging.o asprintf.o service.o time.o obj.o crc32.o

LUABIND_OBJECTS = xFTPd_bind.o

all: proxy.exe xFTPd.exe slave.exe

.c.o:
	$(WCC) -c $*.c

xFTPd_bind.c: xFTPd_bind.pkg
	tolua -o $@ $<

xFTPd.exe: $(LUABIND_OBJECTS) $(XFTPD_OBJECTS)
	$(WCC) -o xFTPd.exe $(LUABIND_OBJECTS) $(XFTPD_OBJECTS) -lws2_32 -lcrypto -lgdi32 -ltolua -llua -lz -lsqlite3

slave.exe: $(SLAVE_OBJECTS)
	$(WCC) -o slave.exe $(SLAVE_OBJECTS) -lws2_32 -lcrypto -lgdi32 -lz

proxy.exe: $(PROXY_OBJECTS)
	$(WCC) -o proxy.exe $(PROXY_OBJECTS) -lws2_32

clean:
	$(RM) xFTPd.exe slave.exe proxy.exe xFTPd_bind.c *.o
