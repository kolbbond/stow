.PHONY: all configure build test clean

all: build

configure:
	cmake -B build

build: configure
	cmake --build build --config RelWithDebInfo

test: build
	ctest --test-dir build --output-on-failure -C RelWithDebInfo

clean:
	cmake -E rm -rf build
