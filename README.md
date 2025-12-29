# auto reloading gcode viewer

I could not find a program like `f3d --watch` that renders gcode.

## use

    git clone https://github.com/aavogt/gcodeviewer
    cd gcodeviewer
    cmake -Scmake -Bbuild && make -Cbuild -j12
    ./build/gcodeviewer path/to/file.gcode

![flange.jpg](https://aavogt.github.io/gcodeviewer/flange.jpg)

![bar.jpg](https://aavogt.github.io/gcodeviewer/bar.jpg)
