goto q
:h
pause
:q
cls
del test.exe
if exist test.exe goto h
mingw32-make -j4
if not exist test.exe goto h
gdb test.exe
goto h