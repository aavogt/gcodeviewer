watch:
	ls mac/* src/* | entr -r make run

watchtest:
	ls src/*.c src/*.h | entr make test

run: cmake
	./build/gcodeviewer battery-adapter.gcode

test: cmake
	./build/test_selected

watchmac:
	cd mac; watch-code-cells segdistance.mac --reload codegen.mac

src/segdistance.h: mac/segdistance.mac mac/codegen.mac
	cd mac; maxima < segdistance.mac

cmake: src/segdistance.h
	cmake -Bbuild -Scmake && make -Cbuild -j12

.PHONY: watch cmake
.SHELL: /bin/bash
