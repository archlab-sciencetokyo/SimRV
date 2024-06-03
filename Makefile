CXX       = g++
CXXFLAGS  = -O2 -Wall -std=c++11
TARGET    = simrv
OBJS      = main.o machine.o module.o disk.o console.o state.o
HEAD      = console.h define.h disk.h machine.h module.h state.h

.SUFFIXES:
.SUFFIXES: .o .cc
.PHONY: all clean run middle


all: $(TARGET)
	$(MAKE) $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJS): Makefile $(HEAD)

.cpp.o:
	$(CXX) $(CXXFLAGS) -o $@ -c $<

run: $(TARGET)
	./$(TARGET) $(ARGS)

pdf:
	cats -f main.cc machine.cc module.cc state.cc console.cc disk.cc  *.h > code.txt
	a2ps --medium=a4 -f 6.1 code.txt -o  code.ps
	ps2pdf13 -sPAPERSIZE=a4 code.ps
	rm -f code.txt code.ps

docs:
	doxygen Doxyfile

wc:
	wc -l *.cc *.h
clean:
	rm -f *.o *.log init*.txt trace*.txt code.pdf init*.bin instmix.txt
