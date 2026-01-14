TARGET = serial_server

include ../../../makefile_cfg

all: $(TARGET)

$(TARGET):main.c
	$(CC) main.c -o $(TARGET) -lpthread -lrt
	@echo "generate $(TARGET) success!!!"
	@cp -f $(TARGET) $(CMD_PATH)
	@echo -e '\e[1;33m cp -f $(TARGET) $(CMD_PATH) \e[0m'
	
.PHONY:clean cleanall

clean: 
	@rm -f $(TARGET)
cleanall:clean
	-rm -f $(CMD_PATH)/$(TARGET) 

distclean:cleanall 
	
install:
	-cp $(TARGET) $(CMD_PATH)	
