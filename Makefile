##########################################################################################
##### SimCore/RISC-V since 2018-07-05                ArchLab. TokyoTech              #####
##########################################################################################

CXX       = g++
CXXFLAGS  = -O2 -Wall -std=c++11
TARGET    = simrv
OBJS      = main.o machine.o module.o disk.o console.o state.o
HEAD      = console.h define.h disk.h machine.h module.h state.h

.SUFFIXES:
.SUFFIXES: .o .cc
.PHONY: all clean run middle

##########################################################################################
all: $(TARGET)
	$(MAKE) $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJS): Makefile $(HEAD)

.cpp.o:
	$(CXX) $(CXXFLAGS) -o $@ -c $<
##########################################################################################
pdf:
	cats -f main.cc machine.cc module.cc state.cc console.cc disk.cc  *.h > code.txt
	a2ps --medium=a4 -f 6.1 code.txt -o  code.ps
	ps2pdf13 -sPAPERSIZE=a4 code.ps
	rm -f code.txt code.ps

##########################################################################################
run:
	./simrv -m /home/share/FPGA/riscv/bin/binary/bbl.bin -d /home/share/FPGA/riscv/bin/binary/root.bin \
	-c /home/share/FPGA/riscv/bin/binary/devicetree.dtb -x

run3:
	./simrv -c img/simrv.dtb -m img/bbl_v03.bin -d img/root_v03.bin

app:
	./simrv -m img/hello.bin

bin:
	./simrv -m img/bbl_v03.bin -d img/root_v03.bin -b

##########################################################################################
tst:
	./simrv -m img/bbl_v03.bin -d /home/pub/riscv/share_image/root_v03.bin
#	./simrv -m img/bbl_v03.bin -d img/root_v03.bin -q 10m -e 64m  -s
##########################################################################################
image:
	./simrv -m img/bbl_v03.bin -d img/root_v03.bin -i 10m -e 20m

run2:
	./$(TARGET) -m img/bbl_v03.bin
trace:
	./$(TARGET) -m img/bbl_v03.bin -d img/root_v02.bin -t 60m 82m -e 83m
wc:
	wc -l *.cc *.h
clean:
	rm -f *.o *.log init*.txt trace*.txt code.pdf init*.bin instmix.txt
##########################################################################################
