CC = gcc
#CFLAGS = -g -DDEBUG -lm -Wall -I include -I cli -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers
CFLAGS = -g -lm -Wall -I lexicalParser/include -I cli -I compiler -I objectAndClass/include -I utils -I vm -I gc -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers

TARGET = stove
DIRS = lexicalParser/include cli objectAndClass/include utils vm compiler gc
CFILES = $(foreach dir,$(DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst %.c,%.o,$(CFILES))

$(TARGET):$(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(CFLAGS)

clean:
	-del $(TARGET) $(OBJS)
	-for %%d in ($(DIRS)) do (del /Q /S "%%d\*.o")
	-del $(TARGET).exe
r: clean $(TARGET)
