CC       = gcc
CFLAGS   = -g -Wall -O2
INCLUDES = 

TARGET1 = server
TARGET2 = client
TARGET3 = init_connections_client
TARGET4 = init_connections_server

LIBS = lib_socket.o

all: $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4)

$(TARGET1): $(TARGET1).o $(LIBS)
	$(CC) $(CFLAGS) -o $(TARGET1) $(TARGET1).o $(LIBS)

$(TARGET2): $(TARGET2).o $(LIBS)
	$(CC) $(CFLAGS) -o $(TARGET2) $(TARGET2).o $(LIBS)

$(TARGET3): $(TARGET3).o $(LIBS)
	$(CC) $(CFLAGS) -o $(TARGET3) $(TARGET3).o $(LIBS)

$(TARGET4): $(TARGET4).o $(LIBS)
	$(CC) $(CFLAGS) -o $(TARGET4) $(TARGET4).o $(LIBS)

%.o:%.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4) *.o *~ core 

.PHONY: clean
