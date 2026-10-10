CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Icore -Iroundkit/include

CORE := core/nowplaying.cpp core/deck.cpp core/scene.cpp roundkit/src/font.cpp
HDRS := $(wildcard core/*.hpp roundkit/include/rk/*.hpp)
TESTS := host/build/test_nowplaying host/build/test_deck host/build/test_scene

.PHONY: test sim shots build flash clean

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

host/build/test_%: host/test_%.cpp $(CORE) $(HDRS)
	@mkdir -p host/build
	$(CXX) $(CXXFLAGS) $< $(CORE) -o $@

sim: host/build/sim
	./host/build/sim

shots: host/build/sim
	@mkdir -p host/out
	./host/build/sim --shots host/out

host/build/sim: host/sim.cpp $(CORE) $(HDRS)
	@mkdir -p host/build
	$(CXX) $(CXXFLAGS) $$(sdl2-config --cflags) host/sim.cpp $(CORE) $$(sdl2-config --libs) -o $@

build:
	./roundkit/scripts/idf set-target esp32s3
	./roundkit/scripts/idf build

flash:
	PORT=$(PORT) ./roundkit/scripts/idf -p $(PORT) flash

clean:
	rm -rf build host/build host/out
