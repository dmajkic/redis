ISCC?= '/c/Program Files (x86)/Inno Setup 5/ISCC.exe'

ifneq (,$(shell objdump -f ../src/redis-server.exe | grep x86-64))
ISCCOPT+= -d_WIN64
endif

all: setup

setup:
	strip ../src/*.{exe,dll}
	$(ISCC) redis.iss $(ISCCOPT)

clean:
	rm -f redis-setup-*.exe
