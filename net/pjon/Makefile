PJON ?= ../../../PJON
mp-pjon-beacon: mp-pjon-beacon.cpp
	$(CXX) -DLINUX -I$(PJON)/src -std=c++14 -O2 $< -o $@
clean:
	rm -f mp-pjon-beacon
