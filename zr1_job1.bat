goto q
:h
pause
:q
cls
del arlibtest.exe
if exist arlibtest.exe goto h
mingw32-make -j1
arlibtest.exe
goto h