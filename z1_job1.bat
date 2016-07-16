goto q
:h
pause
:q
cls
del test.exe
if exist test.exe goto h
mingw32-make -j1
test.exe
goto h