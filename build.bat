@echo off

call .\setup.bat

set BuildFolder=.\build
set CommonCompilerFlags=-MTd -nologo -GR- -EHa- -Oi -W4 -FC -Z7
set CommonLinkerFlags=-opt:ref

if "%1"=="debug" (
	set FinalFlags=%CommonCompilerFlags% -Od
) else (
	set FinalFlags=%CommonCompilerFlags% -O2
)
echo %FinalFlags%
IF NOT EXIST %BuildFolder% mkdir %BuildFolder%
pushd %BuildFolder%
del *.pdb > nul 2> nul
cl %CommonCompilerFlags% ..\pandemic_model.cpp /link %CommonLinkerFlags%
popd