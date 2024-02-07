CC = gcc
#CFLAGS = -g -DDEBUG -lm -Wall -I include -I cli -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers
CFLAGS = -g -lm -Wall -I ./LexicalParser/include -I ./LexicalParser/cli -I ./objectAndClass/include -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers

TARGET = stove
DIRS = ./LexicalParser/include ./LexicalParser/cli ./objectAndClass/include
CFILES = $(foreach dir,$(DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst %.c,%.o,$(CFILES))

$(TARGET):$(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(CFLAGS)

clean:
	-del $(TARGET) $(OBJS)
	-for %%d in ($(DIRS)) do (del /Q /S "%%d\*.o")
	-del $(TARGET).exe
r: clean $(TARGET)
