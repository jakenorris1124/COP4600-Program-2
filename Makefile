obj-m += pa2_in.o pa2_out.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	gcc test.py -o test

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm test
