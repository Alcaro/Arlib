goto q
:h
pause
:q
cls
del test.exe
if exist test.exe goto h
mingw32-make -j4 -k
test.exe
goto h