SRCDIR = src
BUILDDIR = build

obj-m += $(BUILDDIR)/fake_rtc.o

all: $(SRCDIR) $(BUILDDIR)
	cp $(SRCDIR)/*.c $(BUILDDIR)
	cd $(BUILDDIR)
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	cd ..
	cp $(BUILDDIR)/fake_rtc.ko fake_rtc.ko

clean:
	rm -r $(BUILDDIR)
	rm modules.order
	rm Module.symvers

$(BUILDDIR):
	mkdir $(BUILDDIR)

$(SRCDIR):
	$(error Can not find sources dir)