goto q
:h
pause
:q
cls
del arlibtest.exe
if exist arlibtest.exe goto h
mingw32-make -j4
if not exist arlibtest.exe goto h
gdb arlibtest.exe
goto h