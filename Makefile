EXCUTABLE_FILES = ssu_sfinder 
LARGE_OPTION = -D_LARGEFILE64_SOURCE
LIB_OPTION = -lpthread -lcrypto

ok : $(EXCUTABLE_FILES)
	@echo compile success!

ssu_sfinder: ssu_sfinder.c path_queue.o 
	gcc $(LARGE_OPTION) -o $@ ssu_sfinder.c path_queue.o $(LIB_OPTION)

path_queue.o: path_queue.c path_queue.h
	gcc -c path_queue.c

clean:
	rm path_queue.o $(EXCUTABLE_FILES)

