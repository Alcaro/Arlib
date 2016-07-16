#make clean
#make OPT=1 PROFILE=gen -j8
#DISPLAY=foo ./test
#make clean-prof
#make OPT=1 PROFILE=use -j8

FLAGS='-Os -fomit-frame-pointer -fmerge-all-constants -fvisibility=hidden'
FLAGS+=' -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables'
FLAGS+=' -ffunction-sections -fdata-sections'

rm test.exe test64.exe
mingwver 5.3
wine gcc -xc $FLAGS -s -Wl,--gc-sections main.cpp arlib/wutf/*.cpp -o test.exe
mingwver 5.3-64
wine gcc -xc $FLAGS -s -Wl,--gc-sections main.cpp arlib/wutf/*.cpp -o test64.exe
mingwver 5.3
