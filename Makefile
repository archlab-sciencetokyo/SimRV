CXX       = g++
CXXFLAGS  = -O2 -Wall -std=c++11
TARGET    = simrv
SRCDIR    = .
BUILDDIR  = build
OBJS      = $(addprefix $(BUILDDIR)/, main.o machine.o module.o disk.o console.o state.o)
HEAD      = console.h define.h disk.h machine.h module.h state.h

.SUFFIXES: .o .cc
.PHONY: all clean run middle

all: $(BUILDDIR) $(BUILDDIR)/$(TARGET)

simrv: $(BUILDDIR)/$(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.cc Makefile $(HEAD)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

run: $(BUILDDIR)/$(TARGET)
	./$(BUILDDIR)/$(TARGET) $(ARGS)

pdf:
	cats -f $(SRCDIR)/*.cc > code.txt
	a2ps --medium=a4 -f 6.1 code.txt -o  code.ps
	ps2pdf13 -sPAPERSIZE=a4 code.ps
	rm -f code.txt code.ps

docs:
	doxygen Doxyfile

wc:
	wc -l $(SRCDIR)/*.cc $(SRCDIR)/*.h

clean:
	rm -f $(BUILDDIR)/*.o $(BUILDDIR)/$(TARGET) *.log init*.txt trace*.txt code.pdf init*.bin instmix.txt
